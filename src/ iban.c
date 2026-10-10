#include "iban.h"

#include <ctype.h>
#include <string.h>

typedef struct {
    const char *code;
    int length;
} CountryLength;

/* IBAN lengths for a selection of countries (source: SWIFT IBAN registry). */
static const CountryLength COUNTRY_LENGTHS[] = {
    {"AT", 20}, {"BE", 16}, {"CH", 21}, {"DE", 22}, {"DK", 18},
    {"ES", 24}, {"FI", 18}, {"FR", 27}, {"GB", 22}, {"GR", 27},
    {"IE", 22}, {"IR", 26}, {"IT", 27}, {"LU", 20}, {"NL", 18},
    {"NO", 15}, {"PL", 28}, {"PT", 25}, {"SE", 24}, {"TR", 26},
    {"AE", 23},
};

static int expected_length(const char *country)
{
    size_t n = sizeof COUNTRY_LENGTHS / sizeof COUNTRY_LENGTHS[0];
    for (size_t i = 0; i < n; i++) {
        if (strncmp(COUNTRY_LENGTHS[i].code, country, 2) == 0) {
            return COUNTRY_LENGTHS[i].length;
        }
    }
    return -1;
}

int iban_normalize(const char *in, char *out, int out_size)
{
    int len = 0;
    for (const char *p = in; *p != '\0'; p++) {
        if (*p == ' ') {
            continue;
        }
        if (len >= out_size - 1) {
            return -1;
        }
        out[len++] = (char)toupper((unsigned char)*p);
    }
    out[len] = '\0';
    return len;
}

/* Feeds one character into the running remainder. */
static int mod97_step(int remainder, char c)
{
    if (c >= '0' && c <= '9') {
        return (remainder * 10 + (c - '0')) % 97;
    }
    if (c >= 'A' && c <= 'Z') {
        /* letters become two-digit numbers: A = 10 ... Z = 35 */
        return (remainder * 100 + (c - 'A' + 10)) % 97;
    }
    return -1;
}

int iban_mod97(const char *normalized)
{
    size_t len = strlen(normalized);
    if (len < 5) {
        return -1;
    }

    /*
     * The number can have more than 60 digits, far too large for any
     * integer type. We therefore process it digit by digit and keep only
     * the remainder: (a * 10 + b) mod 97 = ((a mod 97) * 10 + b) mod 97.
     */
    int remainder = 0;
    for (size_t i = 4; i < len; i++) {
        remainder = mod97_step(remainder, normalized[i]);
        if (remainder < 0) {
            return -1;
        }
    }
    for (size_t i = 0; i < 4; i++) {
        remainder = mod97_step(remainder, normalized[i]);
        if (remainder < 0) {
            return -1;
        }
    }
    return remainder;
}

IbanResult iban_validate(const char *iban)
{
    char buf[IBAN_MAX_LEN + 1];
    int len = iban_normalize(iban, buf, sizeof buf);
    if (len < 0) {
        return IBAN_BAD_LENGTH;
    }

    /* two letters (country), two digits (check digits), then alphanumeric */
    if (len < 5 || !isupper((unsigned char)buf[0]) || !isupper((unsigned char)buf[1]) ||
        !isdigit((unsigned char)buf[2]) || !isdigit((unsigned char)buf[3])) {
        return IBAN_BAD_CHARS;
    }
    for (int i = 4; i < len; i++) {
        if (!isupper((unsigned char)buf[i]) && !isdigit((unsigned char)buf[i])) {
            return IBAN_BAD_CHARS;
        }
    }

    int expected = expected_length(buf);
    if (expected < 0) {
        return IBAN_UNKNOWN_COUNTRY;
    }
    if (len != expected) {
        return IBAN_BAD_LENGTH;
    }

    return iban_mod97(buf) == 1 ? IBAN_OK : IBAN_BAD_CHECKSUM;
}

const char *iban_result_str(IbanResult result)
{
    switch (result) {
    case IBAN_OK:              return "valid";
    case IBAN_BAD_CHARS:       return "invalid characters";
    case IBAN_BAD_LENGTH:      return "wrong length";
    case IBAN_UNKNOWN_COUNTRY: return "unsupported country";
    case IBAN_BAD_CHECKSUM:    return "bad checksum";
    }
    return "unknown";
}
