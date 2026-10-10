#ifndef TRANSACTION_H
#define TRANSACTION_H

#include <stddef.h>

#define TX_ID_MAX   32
#define TX_IBAN_MAX 40
#define TX_TIME_LEN 17 /* "YYYY-MM-DD HH:MM" + '\0' */

typedef struct {
    char id[TX_ID_MAX];
    char timestamp[TX_TIME_LEN]; /* original text, used for output   */
    long minutes;                /* minutes since 1970-01-01 00:00   */
    char sender[TX_IBAN_MAX];
    char receiver[TX_IBAN_MAX];
    long long amount_cents;      /* money is stored in cents, never as float */
} Transaction;

/* Dynamic array that grows when it is full. */
typedef struct {
    Transaction *items;
    size_t count;
    size_t capacity;
} TransactionList;

/*
 * Loads transactions from a CSV file with the header
 *   id,timestamp,sender_iban,receiver_iban,amount_eur
 * Malformed lines are skipped and counted in *bad_lines.
 * Returns 0 on success, -1 if the file cannot be read or memory runs out.
 */
int tx_load_csv(const char *path, TransactionList *list, int *bad_lines);

/* Appends a copy of tx. Returns 0 on success, -1 if out of memory. */
int tx_append(TransactionList *list, const Transaction *tx);

void tx_free(TransactionList *list);

/* "1234.5" -> 123450. Returns 0 on success, -1 on invalid input. */
int parse_amount(const char *text, long long *out_cents);

/* "2027-04-01 13:45" -> minutes since 1970. Returns 0 on success, -1 on error. */
int parse_timestamp(const char *text, long *out_minutes);

/* 123450 -> "1,234.50" */
void format_amount(long long cents, char *out, size_t out_size);

#endif
