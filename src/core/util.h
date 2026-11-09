#ifndef ELEVENLEDGER_CORE_UTIL_H
#define ELEVENLEDGER_CORE_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Copies a string safely, always NUL-terminating when capacity is non-zero. */
void util_str_copy(char *dst, size_t dst_capacity, const char *src);

/* Validates strict UTF-8 (no overlong forms, surrogates, or values > U+10FFFF). */
bool util_utf8_valid(const char *text);
/* Length-aware variant; rejects embedded NUL bytes and truncated sequences. */
bool util_utf8_valid_n(const char *text, size_t length);

/*
 * Validates a user-facing text field after trimming surrounding whitespace.
 * The text must fit in max_bytes (excluding NUL), be valid UTF-8, contain no
 * C0 control characters or DEL, and be non-empty when allow_empty is false.
 */
bool util_text_valid(const char *text, size_t max_bytes, bool allow_empty);

/* Trims ASCII whitespace in place and returns its new start. */
char *util_trim(char *text);

/* Case-insensitive comparison for ASCII identifiers and team names. */
bool util_ascii_iequal(const char *a, const char *b);

bool util_parse_int(const char *text, int min_value, int max_value, int *out);
bool util_parse_double(const char *text, double min_value, double max_value,
                       double *out);
/* Accepts every finite number, with no lower or upper bound. */
bool util_parse_finite_double(const char *text, double *out);

/* YYYY-MM-DD, years 1900-2200 inclusive. */
bool util_date_valid(const char *date);
void util_date_today(char out[11]);

uint32_t util_crc32(const void *data, size_t length);
uint32_t util_crc32_begin(void);
uint32_t util_crc32_extend(uint32_t state, const void *data, size_t length);
uint32_t util_crc32_finish(uint32_t state);

#endif
