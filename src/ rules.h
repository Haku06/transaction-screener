#ifndef RULES_H
#define RULES_H

#include "transaction.h"

typedef enum {
    SEV_LOW = 1,
    SEV_MEDIUM = 2,
    SEV_HIGH = 3
} Severity;

typedef struct {
    const Transaction *tx; /* points into the TransactionList */
    const char *rule;      /* short rule name, e.g. "LARGE_AMOUNT" */
    Severity severity;
    char detail[96];
} Alert;

typedef struct {
    Alert *items;
    size_t count;
    size_t capacity;
} AlertList;

typedef struct {
    long long large_cents;       /* LARGE_AMOUNT: at or above this amount       */
    int structuring_percent;     /* STRUCTURING: amounts >= this % of the limit */
    int structuring_min_count;   /*   ... at least this many transactions       */
    long structuring_window_min; /*   ... within this many minutes              */
    int velocity_min_count;      /* VELOCITY: at least this many transactions   */
    long velocity_window_min;    /*   ... within this many minutes              */
} RuleConfig;

void rules_default_config(RuleConfig *cfg);

/*
 * Runs all rules. Sorts txs by sender and time first (needed for the
 * time-window rules). Returns 0 on success, -1 if out of memory.
 */
int rules_run(TransactionList *txs, const RuleConfig *cfg, AlertList *out);

/* Sorts alerts by severity (highest first), then by time. */
void alerts_sort(AlertList *alerts);

void alerts_free(AlertList *alerts);

const char *severity_str(Severity s);

#endif
