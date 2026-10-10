#define _POSIX_C_SOURCE 200809L /* for getopt */

#include "rules.h"
#include "transaction.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [-t limit_eur] [-w velocity_window_min] <transactions.csv>\n"
            "\n"
            "Screens bank transactions for typical money-laundering patterns.\n"
            "\n"
            "  -t  reporting limit in EUR (default 10000)\n"
            "  -w  time window for the velocity rule in minutes (default 60)\n"
            "  -h  show this help\n"
            "\n"
            "Exit code: 0 = no alerts, 1 = alerts found, 2 = error\n",
            prog);
}

static void print_report(const char *path, size_t tx_count, int bad_lines,
                         const AlertList *alerts)
{
    int per_severity[4] = {0};
    for (size_t i = 0; i < alerts->count; i++) {
        per_severity[alerts->items[i].severity]++;
    }

    printf("Transaction Screening Report\n");
    printf("============================\n");
    printf("File:          %s\n", path);
    printf("Transactions:  %zu (%d malformed line%s skipped)\n", tx_count, bad_lines,
           bad_lines == 1 ? "" : "s");
    printf("Alerts:        %zu (HIGH %d, MEDIUM %d, LOW %d)\n\n", alerts->count,
           per_severity[SEV_HIGH], per_severity[SEV_MEDIUM], per_severity[SEV_LOW]);

    if (alerts->count == 0) {
        printf("No suspicious activity found.\n");
        return;
    }

    printf("%-7s %-18s %-6s %-16s %14s  %s\n", "SEV", "RULE", "TX", "TIME",
           "AMOUNT (EUR)", "DETAIL");
    for (size_t i = 0; i < alerts->count; i++) {
        const Alert *a = &alerts->items[i];
        char amount[32];
        format_amount(a->tx->amount_cents, amount, sizeof amount);
        printf("%-7s %-18s %-6s %-16s %14s  %s\n", severity_str(a->severity), a->rule,
               a->tx->id, a->tx->timestamp, amount, a->detail);
    }
}

int main(int argc, char *argv[])
{
    RuleConfig cfg;
    rules_default_config(&cfg);

    int opt;
    while ((opt = getopt(argc, argv, "t:w:h")) != -1) {
        char *end;
        switch (opt) {
        case 't': {
            long limit = strtol(optarg, &end, 10);
            if (*end != '\0' || limit <= 0) {
                fprintf(stderr, "error: invalid limit '%s'\n", optarg);
                return 2;
            }
            cfg.large_cents = (long long)limit * 100;
            break;
        }
        case 'w': {
            long window = strtol(optarg, &end, 10);
            if (*end != '\0' || window <= 0) {
                fprintf(stderr, "error: invalid window '%s'\n", optarg);
                return 2;
            }
            cfg.velocity_window_min = window;
            break;
        }
        case 'h':
            usage(argv[0]);
            return 0;
        default:
            usage(argv[0]);
            return 2;
        }
    }
    if (optind != argc - 1) {
        usage(argv[0]);
        return 2;
    }
    const char *path = argv[optind];

    TransactionList txs = {0};
    AlertList alerts = {0};
    int bad_lines = 0;
    int exit_code = 2;

    if (tx_load_csv(path, &txs, &bad_lines) != 0) {
        perror(path);
        goto cleanup;
    }
    if (rules_run(&txs, &cfg, &alerts) != 0) {
        fprintf(stderr, "error: out of memory\n");
        goto cleanup;
    }

    alerts_sort(&alerts);
    print_report(path, txs.count, bad_lines, &alerts);
    exit_code = alerts.count > 0 ? 1 : 0;

cleanup:
    alerts_free(&alerts);
    tx_free(&txs);
    return exit_code;
}
