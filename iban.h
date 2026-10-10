#ifndef IBAN_H
#define IBAN_H

/*
 * IBAN validation according to ISO 13616:
 *   1. remove spaces, convert to upper case
 *   2. check characters and the country-specific length
 *   3. move the first four characters to the end
 *   4. replace letters by numbers (A = 10, B = 11, ..., Z = 35)
 *   5. the resulting number modulo 97 must equal 1
 */

#define IBAN_MAX_LEN 34

typedef enum {
    IBAN_OK = 0,
    IBAN_BAD_CHARS,
    IBAN_BAD_LENGTH,
    IBAN_UNKNOWN_COUNTRY,
    IBAN_BAD_CHECKSUM
} IbanResult;

/* Validates an IBAN (spaces allowed, case-insensitive). */
IbanResult iban_validate(const char *iban);

/* Human-readable description of a validation result. */
const char *iban_result_str(IbanResult result);

/*
 * Removes spaces and converts to upper case.
 * Writes at most out_size - 1 characters plus '\0'.
 * Returns the length, or -1 if the input does not fit.
 */
int iban_normalize(const char *in, char *out, int out_size);

/*
 * Computes the ISO 7064 MOD 97-10 remainder of a normalized IBAN
 * (after moving the first four characters to the end).
 * Returns the remainder (0..96), or -1 on an invalid character.
 */
int iban_mod97(const char *normalized);

#endif
