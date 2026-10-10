#include "transaction.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_MAX_LEN 512
#define CSV_FIELDS   5

/* ---------- parsing helpers ---------- */

int parse_amount(const char *text, long long *out_cents)
{
    long long whole = 0;
    int decimals = 0;
    long long fraction = 0;
    const char *p = text;

    if (!isdigit((unsigned char)*p)) {
        return -1; /* empty, negative or garbage */
    }
    while (isdigit((unsigned char)*p)) {
        if (whole > (LLONG_MAX / 100 - 9) / 10) {
            return -1; /* would overflow */
        }
        whole = whole * 10 + (*p - '0');
        p++;
    }
    if (*p == '.') {
        p++;
        while (isdigit((unsigned char)*p)) {
            if (++decimals > 2) {
                return -1; /* more than two decimal places */
            }
            fraction = fraction * 10 + (*p - '0');
            p++;
        }
        if (decimals == 0) {
            return -1; /* "12." */
        }
        if (decimals == 1) {
            fraction *= 10; /* "12.5" means 50 cents */
        }
    }
    if (*p != '\0') {
        return -1;
    }
    *out_cents = whole * 100 + fraction;
    return 0;
}

static int is_leap_year(int y)
{
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

static int days_in_month(int y, int m)
{
    static const int DAYS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return (m == 2 && is_leap_year(y)) ? 29 : DAYS[m - 1];
}

/*
 * Number of days between 1970-01-01 and the given date.
 * Algorithm by Howard Hinnant ("days_from_civil"); it treats the year as
 * starting in March so that the leap day is the last day of the year.
 */
static long days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;                                   /* [0, 399]    */
    long doy = (153L * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; /* [0, 365]    */
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;           /* [0, 146096] */
    return era * 146097 + doe - 719468;
}

int parse_timestamp(const char *text, long *out_minutes)
{
    int y, mo, d, h, mi, consumed = 0;

    if (strlen(text) != TX_TIME_LEN - 1) {
        return -1;
    }
    if (sscanf(text, "%4d-%2d-%2d %2d:%2d%n", &y, &mo, &d, &h, &mi, &consumed) != 5 ||
        consumed != TX_TIME_LEN - 1) {
        return -1;
    }
    if (mo < 1 || mo > 12 || d < 1 || d > days_in_month(y, mo) ||
        h < 0 || h > 23 || mi < 0 || mi > 59) {
        return -1;
    }
    *out_minutes = days_from_civil(y, mo, d) * 24 * 60 + h * 60 + mi;
    return 0;
}

void format_amount(long long cents, char *out, size_t out_size)
{
    char digits[32];
    char grouped[48];
    int n = snprintf(digits, sizeof digits, "%lld", cents / 100);
    int g = 0;

    /* insert a comma every three digits, counted from the right */
    for (int i = 0; i < n; i++) {
        if (i > 0 && (n - i) % 3 == 0) {
            grouped[g++] = ',';
        }
        grouped[g++] = digits[i];
    }
    grouped[g] = '\0';
    snprintf(out, out_size, "%s.%02lld", grouped, cents % 100);
}

/* ---------- dynamic array ---------- */

int tx_append(TransactionList *list, const Transaction *tx)
{
    if (list->count == list->capacity) {
        size_t new_capacity = list->capacity == 0 ? 16 : list->capacity * 2;
        Transaction *grown = realloc(list->items, new_capacity * sizeof *grown);
        if (grown == NULL) {
            return -1;
        }
        list->items = grown;
        list->capacity = new_capacity;
    }
    list->items[list->count++] = *tx;
    return 0;
}

void tx_free(TransactionList *list)
{
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

/* ---------- CSV loading ---------- */

/* Removes trailing '\n' / '\r' (files may come from Windows). */
static void strip_newline(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[--len] = '\0';
    }
}

/*
 * Splits a line at commas in place. Unlike strtok, empty fields are kept,
 * so "a,,b" gives three fields. Returns the number of fields found.
 */
static int split_csv(char *line, char *fields[], int max_fields)
{
    int count = 0;
    char *start = line;
    for (char *p = line;; p++) {
        if (*p == ',' || *p == '\0') {
            int at_end = (*p == '\0');
            if (count < max_fields) {
                fields[count] = start;
            }
            count++;
            *p = '\0';
            if (at_end) {
                break;
            }
            start = p + 1;
        }
    }
    return count;
}

static int copy_field(char *dst, size_t dst_size, const char *src)
{
    size_t len = strlen(src);
    if (len == 0 || len >= dst_size) {
        return -1;
    }
    memcpy(dst, src, len + 1);
    return 0;
}

static int parse_line(char *line, Transaction *tx)
{
    char *f[CSV_FIELDS];
    if (split_csv(line, f, CSV_FIELDS) != CSV_FIELDS) {
        return -1;
    }
    if (copy_field(tx->id, sizeof tx->id, f[0]) != 0 ||
        copy_field(tx->timestamp, sizeof tx->timestamp, f[1]) != 0 ||
        copy_field(tx->sender, sizeof tx->sender, f[2]) != 0 ||
        copy_field(tx->receiver, sizeof tx->receiver, f[3]) != 0) {
        return -1;
    }
    if (parse_timestamp(tx->timestamp, &tx->minutes) != 0) {
        return -1;
    }
    if (parse_amount(f[4], &tx->amount_cents) != 0) {
        return -1;
    }
    return 0;
}

int tx_load_csv(const char *path, TransactionList *list, int *bad_lines)
{
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return -1;
    }

    char line[LINE_MAX_LEN];
    int line_no = 0;
    *bad_lines = 0;

    while (fgets(line, sizeof line, file) != NULL) {
        line_no++;

        /* a line longer than the buffer: skip the rest of it */
        if (strchr(line, '\n') == NULL && !feof(file)) {
            int c;
            while ((c = fgetc(file)) != '\n' && c != EOF) {
            }
            (*bad_lines)++;
            continue;
        }

        strip_newline(line);
        if (line_no == 1 || line[0] == '\0' || line[0] == '#') {
            continue; /* header, empty line or comment */
        }

        Transaction tx;
        if (parse_line(line, &tx) != 0) {
            fprintf(stderr, "warning: skipping malformed line %d\n", line_no);
            (*bad_lines)++;
            continue;
        }
        if (tx_append(list, &tx) != 0) {
            fclose(file);
            return -1;
        }
    }

    fclose(file);
    return 0;
}
