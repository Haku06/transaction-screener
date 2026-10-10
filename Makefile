CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -O2
SRC      = src/iban.c src/transaction.c src/rules.c

.PHONY: all test debug run clean

all: txscreen

txscreen: $(SRC) src/main.c
	$(CC) $(CFLAGS) -o $@ $^

test: $(SRC) tests/test_all.c
	$(CC) $(CFLAGS) -Isrc -o test_all $^
	./test_all

# build with AddressSanitizer and UndefinedBehaviorSanitizer to catch memory bugs
debug: CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -g -O0 -fsanitize=address,undefined
debug: clean txscreen

run: txscreen
	./txscreen data/sample.csv

clean:
	rm -f txscreen test_all
