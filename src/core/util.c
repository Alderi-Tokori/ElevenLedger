#include "core/util.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void util_str_copy(char *dst, size_t dst_capacity, const char *src)
{
    if (dst == NULL || dst_capacity == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    size_t length = strlen(src);
    if (length >= dst_capacity) {
        length = dst_capacity - 1;
    }
    memcpy(dst, src, length);
    dst[length] = '\0';
}

bool util_utf8_valid(const char *text)
{
    if (text == NULL) {
        return false;
    }
    return util_utf8_valid_n(text, strlen(text));
}

bool util_utf8_valid_n(const char *text, size_t length)
{
    if (text == NULL) {
        return false;
    }
    const unsigned char *p = (const unsigned char *)text;
    const unsigned char *end = p + length;
    while (p < end) {
        unsigned char c = *p++;
        if (c == 0) {
            return false;
        }
        if (c < 0x80) {
            continue;
        }
        size_t remaining = (size_t)(end - p);
        if (c >= 0xC2 && c <= 0xDF) {
            if (remaining < 1 || p[0] < 0x80 || p[0] > 0xBF) {
                return false;
            }
            p += 1;
        } else if (c == 0xE0) {
            if (remaining < 2 || p[0] < 0xA0 || p[0] > 0xBF ||
                p[1] < 0x80 || p[1] > 0xBF) {
                return false;
            }
            p += 2;
        } else if ((c >= 0xE1 && c <= 0xEC) || (c >= 0xEE && c <= 0xEF)) {
            if (remaining < 2 || p[0] < 0x80 || p[0] > 0xBF ||
                p[1] < 0x80 || p[1] > 0xBF) {
                return false;
            }
            p += 2;
        } else if (c == 0xED) {
            if (remaining < 2 || p[0] < 0x80 || p[0] > 0x9F ||
                p[1] < 0x80 || p[1] > 0xBF) {
                return false;
            }
            p += 2;
        } else if (c == 0xF0) {
            if (remaining < 3 || p[0] < 0x90 || p[0] > 0xBF ||
                p[1] < 0x80 || p[1] > 0xBF ||
                p[2] < 0x80 || p[2] > 0xBF) {
                return false;
            }
            p += 3;
        } else if (c >= 0xF1 && c <= 0xF3) {
            if (remaining < 3 || p[0] < 0x80 || p[0] > 0xBF ||
                p[1] < 0x80 || p[1] > 0xBF ||
                p[2] < 0x80 || p[2] > 0xBF) {
                return false;
            }
            p += 3;
        } else if (c == 0xF4) {
            if (remaining < 3 || p[0] < 0x80 || p[0] > 0x8F ||
                p[1] < 0x80 || p[1] > 0xBF ||
                p[2] < 0x80 || p[2] > 0xBF) {
                return false;
            }
            p += 3;
        } else {
            return false;
        }
    }
    return true;
}

bool util_text_valid(const char *text, size_t max_bytes, bool allow_empty)
{
    if (text == NULL || !util_utf8_valid(text)) {
        return false;
    }
    size_t length = strlen(text);
    if (length > max_bytes) {
        return false;
    }
    if (length == 0) {
        return allow_empty;
    }
    if (isspace((unsigned char)text[0]) ||
        isspace((unsigned char)text[length - 1])) {
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)text[i];
        if ((c < 0x20) || c == 0x7F) {
            return false;
        }
    }
    return true;
}

char *util_trim(char *text)
{
    if (text == NULL) {
        return NULL;
    }
    while (*text != '\0' && isspace((unsigned char)*text)) {
        ++text;
    }
    if (*text == '\0') {
        return text;
    }
    char *end = text + strlen(text) - 1;
    while (end > text && isspace((unsigned char)*end)) {
        *end = '\0';
        --end;
    }
    return text;
}

bool util_ascii_iequal(const char *a, const char *b)
{
    if (a == NULL || b == NULL) {
        return false;
    }
    while (*a != '\0' && *b != '\0') {
        unsigned char ca = (unsigned char)*a;
        unsigned char cb = (unsigned char)*b;
        if (ca >= 'A' && ca <= 'Z') {
            ca = (unsigned char)(ca + ('a' - 'A'));
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (unsigned char)(cb + ('a' - 'A'));
        }
        if (ca != cb) {
            return false;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

bool util_parse_int(const char *text, int min_value, int max_value, int *out)
{
    if (text == NULL || out == NULL || *text == '\0') {
        return false;
    }
    errno = 0;
    char *end = NULL;
    long value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        return false;
    }
    if (value < min_value || value > max_value) {
        return false;
    }
    *out = (int)value;
    return true;
}

bool util_parse_double(const char *text, double min_value, double max_value,
                       double *out)
{
    if (text == NULL || out == NULL || *text == '\0') {
        return false;
    }
    errno = 0;
    char *end = NULL;
    double value = strtod(text, &end);
    if (errno != 0 || end == text || *end != '\0' || !isfinite(value)) {
        return false;
    }
    if (value < min_value || value > max_value) {
        return false;
    }
    *out = value;
    return true;
}

bool util_parse_finite_double(const char *text, double *out)
{
    return util_parse_double(text, -INFINITY, INFINITY, out);
}

static bool is_leap_year(int year)
{
    return ((year % 4) == 0 && (year % 100) != 0) || (year % 400) == 0;
}

bool util_date_valid(const char *date)
{
    if (date == NULL || strlen(date) != 10) {
        return false;
    }
    for (int i = 0; i < 10; ++i) {
        if (i == 4 || i == 7) {
            if (date[i] != '-') {
                return false;
            }
        } else if (!isdigit((unsigned char)date[i])) {
            return false;
        }
    }

    int year = (date[0] - '0') * 1000 + (date[1] - '0') * 100 +
               (date[2] - '0') * 10 + (date[3] - '0');
    int month = (date[5] - '0') * 10 + (date[6] - '0');
    int day = (date[8] - '0') * 10 + (date[9] - '0');
    if (year < 1900 || year > 2200 || month < 1 || month > 12 || day < 1) {
        return false;
    }

    static const int days_by_month[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    int max_day = days_by_month[month - 1];
    if (month == 2 && is_leap_year(year)) {
        max_day = 29;
    }
    return day <= max_day;
}

void util_date_today(char out[11])
{
    time_t now = time(NULL);
    struct tm local;
#if defined(_MSC_VER)
    localtime_s(&local, &now);
#else
    struct tm *tmp = localtime(&now);
    if (tmp != NULL) {
        local = *tmp;
    } else {
        memset(&local, 0, sizeof(local));
        local.tm_year = 126;
        local.tm_mon = 0;
        local.tm_mday = 1;
    }
#endif
    char formatted[32];
    snprintf(formatted, sizeof(formatted), "%04d-%02d-%02d",
             local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
    util_str_copy(out, 11, formatted);
}

uint32_t util_crc32_begin(void)
{
    return 0xFFFFFFFFu;
}

uint32_t util_crc32_extend(uint32_t state, const void *data, size_t length)
{
    const unsigned char *bytes = (const unsigned char *)data;
    uint32_t crc = state;
    for (size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit) {
            uint32_t mask = (uint32_t)(-(int32_t)(crc & 1u));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return crc;
}

uint32_t util_crc32_finish(uint32_t state)
{
    return ~state;
}

uint32_t util_crc32(const void *data, size_t length)
{
    return util_crc32_finish(util_crc32_extend(util_crc32_begin(), data, length));
}
