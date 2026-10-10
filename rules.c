#include "rules.h"

#include "iban.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Demo list of high-risk jurisdictions (country codes of the IBAN).
 * Iran is on the FATF "call for action" list. A real system would load
 * the current FATF / EU lists instead of hard-coding them.
 */
static const char *HIGH_RISK_COUNTRIES[] = {"IR"};

void rules_default_config(RuleConfig *cfg)
{
    cfg->large_cents = 10000LL * 100;       /* 10,000 EUR */
    cfg->structuring_percent = 90;          /* 9,000 - 9,999.99 EUR */
    cfg->structuring_min_count = 3;
    cfg->structuring_window_min = 72 * 60;  /* 3 days */
    cfg->velocity_min_count = 5;
    cfg->velocity_window_min = 60;          /* 1 hour */
}

const char *severity_str(Severity s)
{
    switch (s) {
    case SEV_HIGH:   return "HIGH";
    case SEV_MEDIUM: return "MEDIUM";
    case SEV_LOW:    return "LOW";
    }
    return "?";
}

/* ---------- alert list ---------- */

static int add_alert(AlertList *alerts, const Transaction *tx, const char *rule,
                     Severity severity, const char *detail)
{
    if (alerts->count == alerts->capacity) {
        size_t new_capacity = alerts->capacity == 0 ? 16 : alerts->capacity * 2;
        Alert *grown = realloc(alerts->items, new_capacity * sizeof *grown);
        if (grown == NULL) {
            return -1;
        }
        alerts->items = grown;
        alerts->capacity = new_capacity;
    }
    Alert *a = &alerts->items[alerts->count++];
    a->tx = tx;
    a->rule = rule;
    a->severity = severity;
    snprintf(a->detail, sizeof a->detail, "%s", detail);
    return 0;
}

void alerts_free(AlertList *alerts)
{
    free(alerts->items);
    alerts->items = NULL;
    alerts->count = 0;
    alerts->capacity = 0;
}

/* ---------- sorting ---------- */

static int compare_by_sender_then_time(const void *a, const void *b)
{
    const Transaction *x = a;
    const Transaction *y = b;
    int c = strcmp(x->sender, y->sender);
    if (c != 0) {
        return c;
    }
    return (x->minutes > y->minutes) - (x->minutes < y->minutes);
}

static int compare_alerts(const void *a, const void *b)
{
    const Alert *x = a;
    const Alert *y = b;
    if (x->severity != y->severity) {
        return (int)y->severity - (int)x->severity; /* highest first */
    }
    return (x->tx->minutes > y->tx->minutes) - (x->tx->minutes < y->tx->minutes);
}

void alerts_sort(AlertList *alerts)
{
    if (alerts->count < 2) {
        return; /* nothing to sort; also avoids passing NULL to qsort */
    }
    qsort(alerts->items, alerts->count, sizeof alerts->items[0], compare_alerts);
}

/* ---------- rules on a single transaction ---------- */

static int is_high_risk(const char *iban)
{
    size_t n = sizeof HIGH_RISK_COUNTRIES / sizeof HIGH_RISK_COUNTRIES[0];
    for (size_t i = 0; i < n; i++) {
        if (strncmp(iban, HIGH_RISK_COUNTRIES[i], 2) == 0) {
            return 1;
        }
    }
    return 0;
}

static int check_single(const Transaction *tx, const RuleConfig *cfg, AlertList *out)
{
    char detail[96];

    IbanResult s = iban_validate(tx->sender);
    IbanResult r = iban_validate(tx->receiver);
    if (s != IBAN_OK) {
        snprintf(detail, sizeof detail, "sender IBAN: %s", iban_result_str(s));
        if (add_alert(out, tx, "INVALID_IBAN", SEV_HIGH, detail) != 0) return -1;
    }
    if (r != IBAN_OK) {
        snprintf(detail, sizeof detail, "receiver IBAN: %s", iban_result_str(r));
        if (add_alert(out, tx, "INVALID_IBAN", SEV_HIGH, detail) != 0) return -1;
    }

    if (tx->amount_cents >= cfg->large_cents) {
        char limit[32];
        format_amount(cfg->large_cents, limit, sizeof limit);
        snprintf(detail, sizeof detail, "at or above %s EUR limit", limit);
        if (add_alert(out, tx, "LARGE_AMOUNT", SEV_MEDIUM, detail) != 0) return -1;
    }

    if (is_high_risk(tx->receiver)) {
        snprintf(detail, sizeof detail, "receiver in high-risk country %.2s", tx->receiver);
        if (add_alert(out, tx, "HIGH_RISK_COUNTRY", SEV_HIGH, detail) != 0) return -1;
    }
    return 0;
}

/* ---------- rules over a time window ---------- */

/* 4320 -> "3 days", 120 -> "2 h", 45 -> "45 min" */
static void format_duration(long minutes, char *out, size_t out_size)
{
    if (minutes % (24 * 60) == 0) {
        long days = minutes / (24 * 60);
        snprintf(out, out_size, "%ld day%s", days, days == 1 ? "" : "s");
    } else if (minutes % 60 == 0) {
        snprintf(out, out_size, "%ld h", minutes / 60);
    } else {
        snprintf(out, out_size, "%ld min", minutes);
    }
}

/*
 * Sliding window over the transactions idx[0..n-1] of ONE sender, sorted
 * by time. Two pointers (left, right) mark the window; when it contains at
 * least min_count transactions within window_min minutes, every transaction
 * in it is flagged once. Runs in O(n) because each pointer only moves forward.
 */
static int flag_windows(const Transaction *txs, const size_t *idx, size_t n,
                        int min_count, long window_min, unsigned char *flagged,
                        const char *rule, Severity severity, const char *what,
                        AlertList *out)
{
    size_t left = 0;
    for (size_t right = 0; right < n; right++) {
        while (txs[idx[right]].minutes - txs[idx[left]].minutes > window_min) {
            left++;
        }
        size_t in_window = right - left + 1;
        if (in_window >= (size_t)min_count) {
            char detail[96];
            char duration[24];
            format_duration(window_min, duration, sizeof duration);
            snprintf(detail, sizeof detail, "%zu %s within %s", in_window, what, duration);
            for (size_t k = left; k <= right; k++) {
                if (!flagged[k]) {
                    flagged[k] = 1;
                    if (add_alert(out, &txs[idx[k]], rule, severity, detail) != 0) {
                        return -1;
                    }
                }
            }
        }
    }
    return 0;
}

static int check_sender_group(const TransactionList *txs, size_t start, size_t end,
                              const RuleConfig *cfg, AlertList *out)
{
    size_t n = end - start;
    size_t *all = malloc(n * sizeof *all);
    size_t *near = malloc(n * sizeof *near);
    unsigned char *flagged = calloc(n, 1);
    int rc = -1;

    if (all == NULL || near == NULL || flagged == NULL) {
        goto cleanup;
    }

    long long near_limit = cfg->large_cents * cfg->structuring_percent / 100;
    size_t near_count = 0;
    for (size_t i = 0; i < n; i++) {
        all[i] = start + i;
        long long amount = txs->items[start + i].amount_cents;
        if (amount >= near_limit && amount < cfg->large_cents) {
            near[near_count++] = start + i;
        }
    }

    /* STRUCTURING: several payments just below the limit ("smurfing") */
    if (flag_windows(txs->items, near, near_count, cfg->structuring_min_count,
                     cfg->structuring_window_min, flagged, "STRUCTURING", SEV_HIGH,
                     "payments just below limit", out) != 0) {
        goto cleanup;
    }

    /* VELOCITY: unusually many payments in a short time */
    memset(flagged, 0, n);
    if (flag_windows(txs->items, all, n, cfg->velocity_min_count,
                     cfg->velocity_window_min, flagged, "VELOCITY", SEV_MEDIUM,
                     "payments", out) != 0) {
        goto cleanup;
    }
    rc = 0;

cleanup:
    free(all);
    free(near);
    free(flagged);
    return rc;
}

/* ---------- entry point ---------- */

int rules_run(TransactionList *txs, const RuleConfig *cfg, AlertList *out)
{
    if (txs->count == 0) {
        return 0;
    }

    qsort(txs->items, txs->count, sizeof txs->items[0], compare_by_sender_then_time);

    for (size_t i = 0; i < txs->count; i++) {
        if (check_single(&txs->items[i], cfg, out) != 0) {
            return -1;
        }
    }

    /* after sorting, all transactions of one sender are next to each other */
    size_t start = 0;
    for (size_t i = 1; i <= txs->count; i++) {
        if (i == txs->count || strcmp(txs->items[i].sender, txs->items[start].sender) != 0) {
            if (check_sender_group(txs, start, i, cfg, out) != 0) {
                return -1;
            }
            start = i;
        }
    }
    return 0;
}
