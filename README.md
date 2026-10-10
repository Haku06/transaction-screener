# Transaction Screener

A command-line tool written in C that screens bank transactions for typical
money-laundering patterns, similar to the first stage of the transaction
monitoring that banks run as part of their KYC/AML compliance.

```
$ ./txscreen data/sample.csv
Transaction Screening Report
============================
File:          data/sample.csv
Transactions:  22 (1 malformed line skipped)
Alerts:        12 (HIGH 5, MEDIUM 7, LOW 0)

SEV     RULE               TX     TIME               AMOUNT (EUR)  DETAIL
HIGH    STRUCTURING        T010   2027-03-06 09:10       9,500.00  3 payments just below limit within 3 days
HIGH    HIGH_RISK_COUNTRY  T019   2027-03-10 11:00       4,800.00  receiver in high-risk country IR
HIGH    INVALID_IBAN       T020   2027-03-11 15:30         650.00  receiver IBAN: bad checksum
MEDIUM  LARGE_AMOUNT       T009   2027-03-05 10:30      15,000.00  at or above 10,000.00 EUR limit
MEDIUM  VELOCITY           T013   2027-03-09 14:00          93.00  5 payments within 1 h
...
```

## Rules

| Rule | Severity | Triggers when |
| --- | --- | --- |
| `INVALID_IBAN` | HIGH | sender or receiver IBAN fails the ISO 13616 check (characters, country length, MOD 97 checksum) |
| `STRUCTURING` | HIGH | one sender makes at least 3 payments between 90 % and 100 % of the limit within 3 days ("smurfing") |
| `HIGH_RISK_COUNTRY` | HIGH | the receiver's IBAN belongs to a high-risk jurisdiction (demo list) |
| `LARGE_AMOUNT` | MEDIUM | a single payment is at or above the limit (default 10,000 EUR) |
| `VELOCITY` | MEDIUM | one sender makes at least 5 payments within 60 minutes |

## Build and run

Requires a C11 compiler (gcc or clang) and `make`. Works on Linux and macOS.

```bash
make               # builds ./txscreen
make run           # runs it on data/sample.csv
make test          # builds and runs the unit tests
make debug         # builds with AddressSanitizer + UBSan
```

```
Usage: ./txscreen [-t limit_eur] [-w velocity_window_min] <transactions.csv>
Exit code: 0 = no alerts, 1 = alerts found, 2 = error
```

## Input format

```csv
id,timestamp,sender_iban,receiver_iban,amount_eur
T001,2027-03-01 09:12,DE50526018159083016613,ES1018609139099603082462,1250.00
```

Malformed lines (bad date, bad amount, missing fields) are skipped with a warning.

## Implementation notes

- **IBAN checksum:** an IBAN turns into a number with more than 60 digits,
  too large for any integer type. The checksum is computed digit by digit,
  keeping only the remainder modulo 97.
- **Money as integers:** amounts are stored in cents (`long long`), never as
  `float`/`double`, to avoid rounding errors.
- **Time windows in O(n):** transactions are sorted by sender and time with
  `qsort`; the structuring and velocity rules then use a two-pointer sliding
  window over each sender's transactions.
- **Dates without `<time.h>` pitfalls:** timestamps are converted to minutes
  since 1970 with a closed-form calendar algorithm (no time zones involved).
- **Memory safety:** dynamic arrays grow with `realloc`; every allocation is
  checked and freed. Tested with AddressSanitizer and UBSan.

## Project structure

```
src/iban.c         IBAN normalisation and validation
src/transaction.c  CSV parsing, amounts, timestamps, dynamic array
src/rules.c        screening rules and alert list
src/main.c         command-line interface and report
tests/test_all.c   unit tests
data/sample.csv    sample data containing one case for each rule
```

## Disclaimer

Educational project. The rules and the high-risk country list are
simplified examples and not a substitute for a real AML system.
