/*
 * Minimal unit tests without a framework.
 * Build and run with: make test
 */
#include "iban.h"
#include "rules.h"
#include "transaction.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        checks++;                                                         \
        if (!(cond)) {                                                    \
            failures++;                                                   \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                 \
    } while (0)

static void test_iban(void)
{
    /* well-known valid example IBANs */
    CHECK(iban_validate("DE89370400440532013000") == IBAN_OK);
    CHECK(iban_validate("DE89 3704 0044 0532 0130 00") == IBAN_OK); /* with spaces */
    CHECK(iban_validate("de89370400440532013000") == IBAN_OK);      /* lower case  */
    CHECK(iban_validate("GB82WEST12345698765432") == IBAN_OK);
    CHECK(iban_validate("NL91ABNA0417164300") == IBAN_OK);
    CHECK(iban_validate("ES9121000418450200051332") == IBAN_OK);
    CHECK(iban_validate("FR1420041010050500013M02606") == IBAN_OK);

    CHECK(iban_validate("DE89370400440532013001") == IBAN_BAD_CHECKSUM);
    CHECK(iban_validate("DE8937040044053201300") == IBAN_BAD_LENGTH);
    CHECK(iban_validate("XX89370400440532013000") == IBAN_UNKNOWN_COUNTRY);
    CHECK(iban_validate("DE89-37040044053201300") == IBAN_BAD_CHARS);
    CHECK(iban_validate("") == IBAN_BAD_CHARS);
    CHECK(iban_validate("DE89370400440532013000123456789012345") == IBAN_BAD_LENGTH);
}

static void test_amount(void)
{
    long long c = -1;
    CHECK(parse_amount("1234.56", &c) == 0 && c == 123456);
    CHECK(parse_amount("12.5", &c) == 0 && c == 1250);
    CHECK(parse_amount("7", &c) == 0 && c == 700);
    CHECK(parse_amount("0.01", &c) == 0 && c == 1);
    CHECK(parse_amount("-5.00", &c) != 0);
    CHECK(parse_amount("12.", &c) != 0);
    CHECK(parse_amount("1.234", &c) != 0);
    CHECK(parse_amount("abc", &c) != 0);
    CHECK(parse_amount("", &c) != 0);
    CHECK(parse_amount("99999999999999999999", &c) != 0);

    char buf[32];
    format_amount(123456789, buf, sizeof buf);
    CHECK(strcmp(buf, "1,234,567.89") == 0);
    format_amount(5, buf, sizeof buf);
    CHECK(strcmp(buf, "0.05") == 0);
}

static void test_timestamp(void)
{
    long m = -1;
    CHECK(parse_timestamp("1970-01-01 00:00", &m) == 0 && m == 0);
    CHECK(parse_timestamp("1970-01-02 01:30", &m) == 0 && m == 24 * 60 + 90);
    CHECK(parse_timestamp("2028-02-29 12:00", &m) == 0); /* leap year */
    CHECK(parse_timestamp("2027-02-29 12:00", &m) != 0); /* not a leap year */
    CHECK(parse_timestamp("2027-13-01 10:00", &m) != 0);
    CHECK(parse_timestamp("2027-04-01 24:00", &m) != 0);
    CHECK(parse_timestamp("2027-04-01", &m) != 0);

    long a, b;
    parse_timestamp("2027-03-31 23:30", &a);
    parse_timestamp("2027-04-01 00:15", &b);
    CHECK(b - a == 45); /* across a month boundary */
}

static Transaction make_tx(const char *id, const char *time, const char *sender,
                           const char *receiver, long long cents)
{
    Transaction tx;
    snprintf(tx.id, sizeof tx.id, "%s", id);
    snprintf(tx.timestamp, sizeof tx.timestamp, "%s", time);
    parse_timestamp(time, &tx.minutes);
    snprintf(tx.sender, sizeof tx.sender, "%s", sender);
    snprintf(tx.receiver, sizeof tx.receiver, "%s", receiver);
    tx.amount_cents = cents;
    return tx;
}

static size_t count_rule(const AlertList *alerts, const char *rule)
{
    size_t n = 0;
    for (size_t i = 0; i < alerts->count; i++) {
        if (strcmp(alerts->items[i].rule, rule) == 0) {
            n++;
        }
    }
    return n;
}

static void test_rules(void)
{
    const char *A = "DE89370400440532013000";
    const char *B = "GB82WEST12345698765432";
    TransactionList txs = {0};
    AlertList alerts = {0};
    RuleConfig cfg;
    rules_default_config(&cfg);

    Transaction list[] = {
        /* three payments just below 10,000 within 3 days -> STRUCTURING */
        make_tx("S1", "2027-04-01 09:00", A, B, 950000),
        make_tx("S2", "2027-04-02 09:00", A, B, 960000),
        make_tx("S3", "2027-04-03 09:00", A, B, 990000),
        /* outside the window -> must NOT be flagged */
        make_tx("S4", "2027-04-20 09:00", A, B, 950000),
        /* exactly at the limit -> LARGE_AMOUNT */
        make_tx("L1", "2027-04-05 10:00", B, A, 1000000),
    };
    for (size_t i = 0; i < sizeof list / sizeof list[0]; i++) {
        tx_append(&txs, &list[i]);
    }

    CHECK(rules_run(&txs, &cfg, &alerts) == 0);
    CHECK(count_rule(&alerts, "STRUCTURING") == 3);
    CHECK(count_rule(&alerts, "LARGE_AMOUNT") == 1);
    CHECK(count_rule(&alerts, "INVALID_IBAN") == 0);
    CHECK(count_rule(&alerts, "VELOCITY") == 0);

    alerts_sort(&alerts);
    CHECK(alerts.count > 0 && alerts.items[0].severity == SEV_HIGH);

    alerts_free(&alerts);
    tx_free(&txs);
}

int main(void)
{
    test_iban();
    test_amount();
    test_timestamp();
    test_rules();

    printf("%d/%d checks passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
