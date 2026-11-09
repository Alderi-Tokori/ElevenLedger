#include "core/import.h"

#include "core/util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *start;
    const char *end;
} JsonSpan;

static void set_error(char *error, size_t error_size, const char *format, ...)
{
    if (error == NULL || error_size == 0) {
        return;
    }
    va_list args;
    va_start(args, format);
    vsnprintf(error, error_size, format, args);
    va_end(args);
    error[error_size - 1] = '\0';
}

static const char *find_bytes(const char *haystack, size_t haystack_length,
                              const char *needle, size_t needle_length)
{
    if (needle_length == 0) {
        return haystack;
    }
    if (haystack_length < needle_length) {
        return NULL;
    }
    const char *cursor = haystack;
    size_t remaining = haystack_length;
    while (remaining >= needle_length) {
        const char *found = memchr(cursor, (unsigned char)needle[0], remaining);
        if (found == NULL) {
            return NULL;
        }
        if (memcmp(found, needle, needle_length) == 0) {
            return found;
        }
        size_t skipped = (size_t)(found - cursor) + 1;
        cursor += skipped;
        remaining -= skipped;
    }
    return NULL;
}

static void json_skip_ws(const char **cursor, const char *end)
{
    while (*cursor < end) {
        char value = **cursor;
        if (value != ' ' && value != '\t' && value != '\r' && value != '\n') {
            break;
        }
        ++*cursor;
    }
}

static bool json_skip_string(const char **cursor, const char *end)
{
    const char *p = *cursor;
    if (p >= end || *p != '"') {
        return false;
    }
    ++p;
    while (p < end) {
        unsigned char value = (unsigned char)*p++;
        if (value == '"') {
            *cursor = p;
            return true;
        }
        if (value == '\\') {
            if (p >= end) {
                return false;
            }
            char escaped = *p++;
            switch (escaped) {
            case '"':
            case '\\':
            case '/':
            case 'b':
            case 'f':
            case 'n':
            case 'r':
            case 't':
                break;
            case 'u': {
                for (int i = 0; i < 4; ++i) {
                    if (p >= end) {
                        return false;
                    }
                    char digit = *p++;
                    if (!((digit >= '0' && digit <= '9') ||
                          (digit >= 'a' && digit <= 'f') ||
                          (digit >= 'A' && digit <= 'F'))) {
                        return false;
                    }
                }
                break;
            }
            default:
                return false;
            }
            continue;
        }
        if (value < 0x20) {
            return false;
        }
    }
    return false;
}

static bool json_skip_number(const char **cursor, const char *end)
{
    const char *p = *cursor;
    if (p < end && *p == '-') {
        ++p;
    }
    if (p >= end) {
        return false;
    }
    if (*p == '0') {
        ++p;
    } else if (*p >= '1' && *p <= '9') {
        while (p < end && *p >= '0' && *p <= '9') {
            ++p;
        }
    } else {
        return false;
    }
    if (p < end && *p == '.') {
        ++p;
        if (p >= end || *p < '0' || *p > '9') {
            return false;
        }
        while (p < end && *p >= '0' && *p <= '9') {
            ++p;
        }
    }
    if (p < end && (*p == 'e' || *p == 'E')) {
        ++p;
        if (p < end && (*p == '+' || *p == '-')) {
            ++p;
        }
        if (p >= end || *p < '0' || *p > '9') {
            return false;
        }
        while (p < end && *p >= '0' && *p <= '9') {
            ++p;
        }
    }
    *cursor = p;
    return true;
}

static bool json_skip_literal(const char **cursor, const char *end,
                              const char *literal)
{
    size_t length = strlen(literal);
    if ((size_t)(end - *cursor) < length ||
        memcmp(*cursor, literal, length) != 0) {
        return false;
    }
    *cursor += length;
    return true;
}

static bool json_skip_value(const char **cursor, const char *end, int depth);

static bool json_skip_object(const char **cursor, const char *end, int depth)
{
    const char *p = *cursor;
    if (p >= end || *p != '{') {
        return false;
    }
    ++p;
    json_skip_ws(&p, end);
    if (p < end && *p == '}') {
        *cursor = p + 1;
        return true;
    }
    while (p < end) {
        if (!json_skip_string(&p, end)) {
            return false;
        }
        json_skip_ws(&p, end);
        if (p >= end || *p != ':') {
            return false;
        }
        ++p;
        json_skip_ws(&p, end);
        if (!json_skip_value(&p, end, depth + 1)) {
            return false;
        }
        json_skip_ws(&p, end);
        if (p < end && *p == ',') {
            ++p;
            json_skip_ws(&p, end);
            continue;
        }
        if (p < end && *p == '}') {
            *cursor = p + 1;
            return true;
        }
        return false;
    }
    return false;
}

static bool json_skip_array(const char **cursor, const char *end, int depth)
{
    const char *p = *cursor;
    if (p >= end || *p != '[') {
        return false;
    }
    ++p;
    json_skip_ws(&p, end);
    if (p < end && *p == ']') {
        *cursor = p + 1;
        return true;
    }
    while (p < end) {
        if (!json_skip_value(&p, end, depth + 1)) {
            return false;
        }
        json_skip_ws(&p, end);
        if (p < end && *p == ',') {
            ++p;
            json_skip_ws(&p, end);
            continue;
        }
        if (p < end && *p == ']') {
            *cursor = p + 1;
            return true;
        }
        return false;
    }
    return false;
}

static bool json_skip_value(const char **cursor, const char *end, int depth)
{
    if (depth > 64) {
        return false;
    }
    json_skip_ws(cursor, end);
    if (*cursor >= end) {
        return false;
    }
    char value = **cursor;
    switch (value) {
    case '{':
        return json_skip_object(cursor, end, depth);
    case '[':
        return json_skip_array(cursor, end, depth);
    case '"':
        return json_skip_string(cursor, end);
    case 't':
        return json_skip_literal(cursor, end, "true");
    case 'f':
        return json_skip_literal(cursor, end, "false");
    case 'n':
        return json_skip_literal(cursor, end, "null");
    default:
        if (value == '-' || (value >= '0' && value <= '9')) {
            return json_skip_number(cursor, end);
        }
        return false;
    }
}

static bool json_next_value(const char **cursor, const char *end,
                            JsonSpan *out)
{
    json_skip_ws(cursor, end);
    const char *start = *cursor;
    if (!json_skip_value(cursor, end, 0)) {
        return false;
    }
    out->start = start;
    out->end = *cursor;
    return true;
}

static int json_hex_value(char value)
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

static bool json_read_hex4(const char **cursor, const char *end,
                           unsigned *out)
{
    unsigned value = 0;
    for (int i = 0; i < 4; ++i) {
        if (*cursor >= end) {
            return false;
        }
        int digit = json_hex_value(**cursor);
        if (digit < 0) {
            return false;
        }
        value = value * 16u + (unsigned)digit;
        ++*cursor;
    }
    *out = value;
    return true;
}

static bool utf8_append(char *output, size_t capacity, size_t *used,
                        unsigned codepoint)
{
    unsigned char bytes[4];
    size_t length = 0;
    if (codepoint <= 0x7F) {
        bytes[0] = (unsigned char)codepoint;
        length = 1;
    } else if (codepoint <= 0x7FF) {
        bytes[0] = (unsigned char)(0xC0 | (codepoint >> 6));
        bytes[1] = (unsigned char)(0x80 | (codepoint & 0x3F));
        length = 2;
    } else if (codepoint <= 0xFFFF) {
        if (codepoint >= 0xD800 && codepoint <= 0xDFFF) {
            return false;
        }
        bytes[0] = (unsigned char)(0xE0 | (codepoint >> 12));
        bytes[1] = (unsigned char)(0x80 | ((codepoint >> 6) & 0x3F));
        bytes[2] = (unsigned char)(0x80 | (codepoint & 0x3F));
        length = 3;
    } else if (codepoint <= 0x10FFFF) {
        bytes[0] = (unsigned char)(0xF0 | (codepoint >> 18));
        bytes[1] = (unsigned char)(0x80 | ((codepoint >> 12) & 0x3F));
        bytes[2] = (unsigned char)(0x80 | ((codepoint >> 6) & 0x3F));
        bytes[3] = (unsigned char)(0x80 | (codepoint & 0x3F));
        length = 4;
    } else {
        return false;
    }
    if (*used + length + 1 > capacity) {
        return false;
    }
    memcpy(output + *used, bytes, length);
    *used += length;
    return true;
}

static bool json_decode_string(JsonSpan span, char *output,
                               size_t output_size)
{
    if (output == NULL || output_size == 0 || span.start >= span.end ||
        *span.start != '"') {
        return false;
    }
    const char *p = span.start + 1;
    const char *end = span.end;
    if (end <= p || end[-1] != '"') {
        return false;
    }
    --end;
    size_t used = 0;
    while (p < end) {
        unsigned char value = (unsigned char)*p++;
        if (value == '\\') {
            if (p >= end) {
                return false;
            }
            char escaped = *p++;
            switch (escaped) {
            case '"':
            case '\\':
            case '/':
                if (used + 2 > output_size) {
                    return false;
                }
                output[used++] = escaped;
                break;
            case 'b':
                if (used + 2 > output_size) {
                    return false;
                }
                output[used++] = '\b';
                break;
            case 'f':
                if (used + 2 > output_size) {
                    return false;
                }
                output[used++] = '\f';
                break;
            case 'n':
                if (used + 2 > output_size) {
                    return false;
                }
                output[used++] = '\n';
                break;
            case 'r':
                if (used + 2 > output_size) {
                    return false;
                }
                output[used++] = '\r';
                break;
            case 't':
                if (used + 2 > output_size) {
                    return false;
                }
                output[used++] = '\t';
                break;
            case 'u': {
                unsigned codepoint = 0;
                if (!json_read_hex4(&p, end, &codepoint)) {
                    return false;
                }
                if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
                    if ((size_t)(end - p) < 6 || p[0] != '\\' || p[1] != 'u') {
                        return false;
                    }
                    p += 2;
                    unsigned low = 0;
                    if (!json_read_hex4(&p, end, &low) ||
                        low < 0xDC00 || low > 0xDFFF) {
                        return false;
                    }
                    codepoint = 0x10000u +
                                ((codepoint - 0xD800u) << 10) +
                                (low - 0xDC00u);
                } else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) {
                    return false;
                }
                if (codepoint == 0) {
                    return false;
                }
                if (!utf8_append(output, output_size, &used, codepoint)) {
                    return false;
                }
                break;
            }
            default:
                return false;
            }
        } else {
            if (value < 0x20) {
                return false;
            }
            if (used + 2 > output_size) {
                return false;
            }
            output[used++] = (char)value;
        }
    }
    output[used] = '\0';
    return true;
}

static bool json_object_get(JsonSpan object, const char *key, JsonSpan *out)
{
    if (object.start >= object.end || *object.start != '{') {
        return false;
    }
    const char *p = object.start + 1;
    const char *end = object.end;
    while (p < end) {
        json_skip_ws(&p, end);
        if (p < end && *p == '}') {
            return false;
        }
        JsonSpan key_span;
        if (!json_next_value(&p, end, &key_span) ||
            key_span.start >= key_span.end || *key_span.start != '"') {
            return false;
        }
        char decoded_key[64];
        if (!json_decode_string(key_span, decoded_key,
                                sizeof(decoded_key))) {
            return false;
        }
        json_skip_ws(&p, end);
        if (p >= end || *p != ':') {
            return false;
        }
        ++p;
        JsonSpan value;
        if (!json_next_value(&p, end, &value)) {
            return false;
        }
        if (strcmp(decoded_key, key) == 0) {
            *out = value;
            return true;
        }
        json_skip_ws(&p, end);
        if (p < end && *p == ',') {
            ++p;
            continue;
        }
        return false;
    }
    return false;
}

typedef struct {
    const char *cursor;
    const char *end;
    bool first;
} JsonArrayIter;

static bool json_array_begin(JsonSpan array, JsonArrayIter *iter)
{
    if (array.start >= array.end || *array.start != '[') {
        return false;
    }
    iter->cursor = array.start + 1;
    iter->end = array.end;
    iter->first = true;
    return true;
}

static bool json_array_next(JsonArrayIter *iter, JsonSpan *out)
{
    const char *p = iter->cursor;
    json_skip_ws(&p, iter->end);
    if (p < iter->end && *p == ']') {
        iter->cursor = p + 1;
        return false;
    }
    if (!iter->first) {
        if (p >= iter->end || *p != ',') {
            return false;
        }
        ++p;
        json_skip_ws(&p, iter->end);
    }
    JsonSpan value;
    if (!json_next_value(&p, iter->end, &value)) {
        return false;
    }
    iter->cursor = p;
    iter->first = false;
    *out = value;
    return true;
}

static bool json_parse_bool(JsonSpan span, bool *out)
{
    if ((size_t)(span.end - span.start) == 4 &&
        memcmp(span.start, "true", 4) == 0) {
        *out = true;
        return true;
    }
    if ((size_t)(span.end - span.start) == 5 &&
        memcmp(span.start, "false", 5) == 0) {
        *out = false;
        return true;
    }
    return false;
}

static bool json_parse_int(JsonSpan span, int *out)
{
    const char *p = span.start;
    const char *end = span.end;
    if (p >= end) {
        return false;
    }
    bool negative = false;
    if (*p == '-') {
        negative = true;
        ++p;
    }
    if (p >= end || *p < '0' || *p > '9') {
        return false;
    }
    long value = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        value = value * 10 + (*p - '0');
        if (value > 1000000L) {
            return false;
        }
        ++p;
    }
    if (p < end && *p == '.') {
        ++p;
        if (p >= end || *p != '0') {
            return false;
        }
        ++p;
    }
    if (p != end) {
        return false;
    }
    if (negative) {
        value = -value;
    }
    if (value < -2147483647L - 1 || value > 2147483647L) {
        return false;
    }
    *out = (int)value;
    return true;
}

static void js_skip_ws(const char **cursor, const char *end)
{
    while (*cursor < end) {
        char value = **cursor;
        if (value != ' ' && value != '\t' && value != '\r' && value != '\n') {
            break;
        }
        ++*cursor;
    }
}

static bool js_parse_string(const char **cursor, const char *end,
                            char *output, size_t output_size)
{
    const char *p = *cursor;
    js_skip_ws(&p, end);
    if (p >= end || (*p != '\'' && *p != '"')) {
        return false;
    }
    char quote = *p++;
    size_t used = 0;
    while (p < end) {
        unsigned char value = (unsigned char)*p++;
        if ((char)value == quote) {
            if (output_size == 0) {
                return false;
            }
            output[used] = '\0';
            *cursor = p;
            return true;
        }
        if (value == '\\') {
            if (p >= end) {
                return false;
            }
            char escaped = *p++;
            switch (escaped) {
            case '\\':
            case '\'':
            case '"':
            case '/':
                if (used + 2 > output_size) {
                    return false;
                }
                output[used++] = escaped;
                break;
            case 'b':
                if (used + 2 > output_size) {
                    return false;
                }
                output[used++] = '\b';
                break;
            case 'f':
                if (used + 2 > output_size) {
                    return false;
                }
                output[used++] = '\f';
                break;
            case 'n':
                if (used + 2 > output_size) {
                    return false;
                }
                output[used++] = '\n';
                break;
            case 'r':
                if (used + 2 > output_size) {
                    return false;
                }
                output[used++] = '\r';
                break;
            case 't':
                if (used + 2 > output_size) {
                    return false;
                }
                output[used++] = '\t';
                break;
            case 'u': {
                unsigned codepoint = 0;
                if (!json_read_hex4(&p, end, &codepoint)) {
                    return false;
                }
                if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
                    if ((size_t)(end - p) < 6 || p[0] != '\\' || p[1] != 'u') {
                        return false;
                    }
                    p += 2;
                    unsigned low = 0;
                    if (!json_read_hex4(&p, end, &low) ||
                        low < 0xDC00 || low > 0xDFFF) {
                        return false;
                    }
                    codepoint = 0x10000u +
                                ((codepoint - 0xD800u) << 10) +
                                (low - 0xDC00u);
                } else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) {
                    return false;
                }
                if (codepoint == 0) {
                    return false;
                }
                if (!utf8_append(output, output_size, &used, codepoint)) {
                    return false;
                }
                break;
            }
            default:
                return false;
            }
            continue;
        }
        if (value < 0x20) {
            return false;
        }
        if (used + 2 > output_size) {
            return false;
        }
        output[used++] = (char)value;
    }
    return false;
}

static bool js_skip_array_number(const char **cursor, const char *end)
{
    const char *p = *cursor;
    js_skip_ws(&p, end);
    if (p < end && *p == '-') {
        ++p;
    }
    int digits = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        ++p;
        ++digits;
    }
    if (digits == 0) {
        return false;
    }
    js_skip_ws(&p, end);
    if (p >= end || *p != ',') {
        return false;
    }
    *cursor = p + 1;
    return true;
}

static bool find_match_header_names(const char *html, size_t length,
                                    char *home, size_t home_size,
                                    char *away, size_t away_size)
{
    static const char marker[] = "matchheader";
    static const char input_key[] = "input";
    const char *end = html + length;
    const char *found = find_bytes(html, length, marker,
                                   sizeof(marker) - 1);
    if (found == NULL) {
        return false;
    }
    const char *search = found + sizeof(marker) - 1;
    size_t search_length = (size_t)(end - search);
    if (search_length > 4096) {
        search_length = 4096;
    }
    const char *input = find_bytes(search, search_length, input_key,
                                   sizeof(input_key) - 1);
    if (input == NULL) {
        return false;
    }
    const char *p = input + sizeof(input_key) - 1;
    js_skip_ws(&p, end);
    if (p >= end || *p != ':') {
        return false;
    }
    ++p;
    js_skip_ws(&p, end);
    if (p >= end || *p != '[') {
        return false;
    }
    ++p;
    if (!js_skip_array_number(&p, end) ||
        !js_skip_array_number(&p, end)) {
        return false;
    }
    char home_temp[512];
    char away_temp[512];
    if (!js_parse_string(&p, end, home_temp, sizeof(home_temp))) {
        return false;
    }
    js_skip_ws(&p, end);
    if (p >= end || *p != ',') {
        return false;
    }
    ++p;
    if (!js_parse_string(&p, end, away_temp, sizeof(away_temp))) {
        return false;
    }
    char *home_trimmed = util_trim(home_temp);
    char *away_trimmed = util_trim(away_temp);
    if (!util_text_valid(home_trimmed, TEAM_NAME_MAX, false) ||
        !util_text_valid(away_trimmed, TEAM_NAME_MAX, false)) {
        return false;
    }
    util_str_copy(home, home_size, home_trimmed);
    util_str_copy(away, away_size, away_trimmed);
    return true;
}

static bool decode_trimmed_text(JsonSpan span, size_t max_bytes,
                                char *output, size_t output_size,
                                char *error, size_t error_size,
                                const char *label)
{
    char buffer[1024];
    if (!json_decode_string(span, buffer, sizeof(buffer))) {
        set_error(error, error_size, "The match page has an invalid %s.",
                  label);
        return false;
    }
    char *trimmed = util_trim(buffer);
    if (!util_text_valid(trimmed, max_bytes, false)) {
        set_error(error, error_size, "The match page has an invalid %s.",
                  label);
        return false;
    }
    util_str_copy(output, output_size, trimmed);
    return true;
}

static bool find_match_centre_data(const char *html, size_t length,
                                   JsonSpan *out)
{
    static const char marker[] = "matchCentreData";
    const size_t marker_length = sizeof(marker) - 1;
    const char *end = html + length;
    const char *cursor = html;
    size_t remaining = length;
    while (remaining >= marker_length) {
        const char *found = find_bytes(cursor, remaining, marker,
                                       marker_length);
        if (found == NULL) {
            return false;
        }
        const char *p = found + marker_length;
        json_skip_ws(&p, end);
        if (p < end && *p == ':') {
            ++p;
            JsonSpan span;
            if (json_next_value(&p, end, &span) && span.start < span.end &&
                *span.start == '{') {
                *out = span;
                return true;
            }
        }
        size_t consumed = (size_t)(found - html) + marker_length;
        if (consumed >= length) {
            break;
        }
        cursor = html + consumed;
        remaining = length - consumed;
    }
    return false;
}

static bool parse_score_digits(const char **cursor, int *out)
{
    const char *p = *cursor;
    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    if (*p < '0' || *p > '9') {
        return false;
    }
    int value = 0;
    int digits = 0;
    while (*p >= '0' && *p <= '9') {
        value = value * 10 + (*p - '0');
        ++p;
        ++digits;
        if (digits > 2) {
            return false;
        }
    }
    if (value > 99) {
        return false;
    }
    *cursor = p;
    *out = value;
    return true;
}

static bool parse_score_pair(const char *text, int *home, int *away)
{
    const char *p = text;
    int first = 0;
    int second = 0;
    if (!parse_score_digits(&p, &first)) {
        return false;
    }
    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    if (*p != ':') {
        return false;
    }
    ++p;
    if (!parse_score_digits(&p, &second)) {
        return false;
    }
    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    if (*p != '\0') {
        return false;
    }
    *home = first;
    *away = second;
    return true;
}

static bool parse_fixture_score(JsonSpan root, JsonSpan home, JsonSpan away,
                                int *home_goals, int *away_goals,
                                char *error, size_t error_size)
{
    bool used_numbers = false;
    JsonSpan home_scores;
    JsonSpan away_scores;
    if (json_object_get(home, "scores", &home_scores) &&
        json_object_get(away, "scores", &away_scores)) {
        JsonSpan home_full;
        JsonSpan away_full;
        if (json_object_get(home_scores, "fulltime", &home_full) &&
            json_object_get(away_scores, "fulltime", &away_full)) {
            int home_value = 0;
            int away_value = 0;
            if (json_parse_int(home_full, &home_value) &&
                json_parse_int(away_full, &away_value) &&
                home_value >= 0 && home_value <= 99 &&
                away_value >= 0 && away_value <= 99) {
                *home_goals = home_value;
                *away_goals = away_value;
                used_numbers = true;
            }
        }
    }
    if (used_numbers) {
        return true;
    }

    const char *keys[2] = { "ftScore", "score" };
    for (int i = 0; i < 2; ++i) {
        JsonSpan span;
        if (!json_object_get(root, keys[i], &span) ||
            span.start >= span.end || *span.start != '"') {
            continue;
        }
        char score_text[64];
        if (!json_decode_string(span, score_text, sizeof(score_text))) {
            continue;
        }
        int home_value = 0;
        int away_value = 0;
        if (parse_score_pair(score_text, &home_value, &away_value)) {
            *home_goals = home_value;
            *away_goals = away_value;
            return true;
        }
    }
    set_error(error, error_size,
              "The match page does not contain a final score.");
    return false;
}

static bool parse_fixture_date(JsonSpan root, char *date,
                               char *error, size_t error_size)
{
    const char *keys[2] = { "startDate", "startTime" };
    for (int i = 0; i < 2; ++i) {
        JsonSpan span;
        if (!json_object_get(root, keys[i], &span) ||
            span.start >= span.end || *span.start != '"') {
            continue;
        }
        char text[128];
        if (!json_decode_string(span, text, sizeof(text))) {
            continue;
        }
        char *trimmed = util_trim(text);
        if (strlen(trimmed) >= MATCH_DATE_MAX) {
            memcpy(date, trimmed, MATCH_DATE_MAX);
            date[MATCH_DATE_MAX] = '\0';
            if (util_date_valid(date)) {
                return true;
            }
        }
    }
    set_error(error, error_size,
              "The match page does not contain a valid fixture date.");
    return false;
}

typedef struct {
    int match_length;
    int first_half_end;
    int expanded_offset;
} MatchTiming;

static bool read_positive_int(JsonSpan object, const char *key, int *out)
{
    JsonSpan span;
    int value = 0;
    if (!json_object_get(object, key, &span) ||
        !json_parse_int(span, &value) || value <= 0) {
        return false;
    }
    *out = value;
    return true;
}

static int last_period_end_minute(JsonSpan root)
{
    JsonSpan period_ends;
    if (!json_object_get(root, "periodEndMinutes", &period_ends) ||
        period_ends.start >= period_ends.end ||
        *period_ends.start != '{') {
        return 0;
    }
    static const char *keys[] = { "4", "3", "2", "1" };
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        int value = 0;
        if (read_positive_int(period_ends, keys[i], &value)) {
            return value;
        }
    }
    return 0;
}

static void resolve_match_timing(JsonSpan root, bool extra_time,
                                 MatchTiming *timing)
{
    memset(timing, 0, sizeof(*timing));
    timing->match_length = extra_time ? 120 : 90;
    timing->first_half_end = 45;

    JsonSpan period_ends;
    if (json_object_get(root, "periodEndMinutes", &period_ends) &&
        period_ends.start < period_ends.end &&
        *period_ends.start == '{') {
        int first_half = 0;
        if (read_positive_int(period_ends, "1", &first_half)) {
            timing->first_half_end = first_half;
        }
    }

    /*
     * maxMinute is the elapsed match clock, including stoppage time. The
     * expanded clock adds the first-half stoppage offset back into the second
     * half, so expandedMaxMinute is not the match length. Prefer maxMinute,
     * then the last recorded period end, then the nominal length.
     */
    int raw_length = 0;
    if (!read_positive_int(root, "maxMinute", &raw_length)) {
        raw_length = last_period_end_minute(root);
    }
    if (raw_length <= 0) {
        raw_length = extra_time ? 120 : 90;
    }

    int expanded_max = 0;
    if (read_positive_int(root, "expandedMaxMinute", &expanded_max) &&
        expanded_max > raw_length) {
        timing->expanded_offset = expanded_max - raw_length;
    }

    if (raw_length > 120) {
        raw_length = 120;
    }
    if (raw_length < 1) {
        raw_length = 1;
    }
    timing->match_length = raw_length;
}

static int match_minute_from_expanded(int expanded,
                                      const MatchTiming *timing)
{
    int minute = expanded;
    if (minute > timing->first_half_end && timing->expanded_offset > 0) {
        minute -= timing->expanded_offset;
    }
    if (minute < 1) {
        minute = 1;
    }
    if (minute > 120) {
        minute = 120;
    }
    return minute;
}

typedef struct {
    int player_id;
    int minute;
} SendingOff;

#define IMPORT_MAX_SENDING_OFFS IMPORT_MAX_PLAYERS

static bool json_string_equals(JsonSpan span, const char *value)
{
    if (span.start >= span.end || *span.start != '"') {
        return false;
    }
    char text[64];
    if (!json_decode_string(span, text, sizeof(text))) {
        return false;
    }
    return util_ascii_iequal(text, value);
}

static bool qualifier_is_sending_off(JsonSpan qualifier)
{
    if (qualifier.start >= qualifier.end || *qualifier.start != '{') {
        return false;
    }
    JsonSpan type;
    if (!json_object_get(qualifier, "type", &type) ||
        type.start >= type.end || *type.start != '{') {
        return false;
    }
    JsonSpan value_span;
    int value = 0;
    if (json_object_get(type, "value", &value_span) &&
        json_parse_int(value_span, &value) &&
        (value == 32 || value == 33)) {
        return true;
    }
    JsonSpan name;
    if (!json_object_get(type, "displayName", &name)) {
        return false;
    }
    return json_string_equals(name, "Red") ||
           json_string_equals(name, "SecondYellow") ||
           json_string_equals(name, "YellowRed");
}

static void add_sending_off(SendingOff *sending_offs, size_t *count,
                            size_t capacity, int player_id, int minute)
{
    if (player_id <= 0 || minute <= 0) {
        return;
    }
    for (size_t i = 0; i < *count; ++i) {
        if (sending_offs[i].player_id == player_id) {
            if (minute < sending_offs[i].minute) {
                sending_offs[i].minute = minute;
            }
            return;
        }
    }
    if (*count >= capacity) {
        return;
    }
    sending_offs[*count].player_id = player_id;
    sending_offs[*count].minute = minute;
    ++*count;
}

static void collect_sending_offs(JsonSpan root, SendingOff *sending_offs,
                                 size_t *out_count, size_t capacity)
{
    *out_count = 0;
    JsonSpan events;
    if (!json_object_get(root, "events", &events)) {
        return;
    }
    JsonArrayIter iter;
    if (!json_array_begin(events, &iter)) {
        return;
    }
    JsonSpan event;
    while (json_array_next(&iter, &event)) {
        if (event.start >= event.end || *event.start != '{') {
            continue;
        }
        JsonSpan type;
        if (!json_object_get(event, "type", &type) ||
            type.start >= type.end || *type.start != '{') {
            continue;
        }
        JsonSpan name;
        if (!json_object_get(type, "displayName", &name) ||
            !json_string_equals(name, "Card")) {
            continue;
        }
        JsonSpan number;
        int player_id = 0;
        int minute = 0;
        if (!json_object_get(event, "playerId", &number) ||
            !json_parse_int(number, &player_id) ||
            !json_object_get(event, "minute", &number) ||
            !json_parse_int(number, &minute)) {
            continue;
        }
        JsonSpan qualifiers;
        if (!json_object_get(event, "qualifiers", &qualifiers)) {
            continue;
        }
        JsonArrayIter qualifier_iter;
        if (!json_array_begin(qualifiers, &qualifier_iter)) {
            continue;
        }
        JsonSpan qualifier;
        bool sending_off = false;
        while (json_array_next(&qualifier_iter, &qualifier)) {
            if (qualifier_is_sending_off(qualifier)) {
                sending_off = true;
                break;
            }
        }
        if (sending_off) {
            add_sending_off(sending_offs, out_count, capacity, player_id,
                            minute);
        }
    }
}

static int sending_off_minute(const SendingOff *sending_offs, size_t count,
                              int player_id)
{
    for (size_t i = 0; i < count; ++i) {
        if (sending_offs[i].player_id == player_id) {
            return sending_offs[i].minute;
        }
    }
    return 0;
}

static bool split_player_name(const char *full_name, ImportedPlayer *player,
                              char *error, size_t error_size)
{
    char temp[512];
    util_str_copy(temp, sizeof(temp), full_name);
    char *trimmed = util_trim(temp);
    size_t full_length = strlen(trimmed);
    if (full_length == 0 || full_length > PERSON_NAME_MAX * 2 + 1 ||
        !util_text_valid(trimmed, PERSON_NAME_MAX * 2 + 1, false)) {
        set_error(error, error_size,
                  "The match page contains an invalid player name.");
        return false;
    }

    char first[PERSON_NAME_MAX * 2 + 1];
    char last[PERSON_NAME_MAX * 2 + 1];
    util_str_copy(first, sizeof(first), trimmed);
    last[0] = '\0';
    char *space = strchr(first, ' ');
    if (space != NULL) {
        *space = '\0';
        util_str_copy(last, sizeof(last), space + 1);
        char *last_trimmed = util_trim(last);
        memmove(last, last_trimmed, strlen(last_trimmed) + 1);
        char *first_trimmed = util_trim(first);
        memmove(first, first_trimmed, strlen(first_trimmed) + 1);
    }
    if (!util_text_valid(first, PERSON_NAME_MAX, true) ||
        !util_text_valid(last, PERSON_NAME_MAX, true) ||
        (first[0] == '\0' && last[0] == '\0')) {
        set_error(error, error_size,
                  "The match page contains an invalid player name.");
        return false;
    }
    util_str_copy(player->first_name, sizeof(player->first_name), first);
    util_str_copy(player->last_name, sizeof(player->last_name), last);
    return true;
}

static bool grow_imported_players(ImportedPlayer **players, size_t *capacity,
                                  size_t needed)
{
    if (needed <= *capacity) {
        return true;
    }
    size_t next = *capacity == 0 ? 16 : *capacity * 2;
    while (next < needed) {
        next *= 2;
    }
    ImportedPlayer *grown = realloc(*players, next * sizeof(*grown));
    if (grown == NULL) {
        return false;
    }
    *players = grown;
    *capacity = next;
    return true;
}

static bool parse_team_players(JsonSpan team, const MatchTiming *timing,
                               const SendingOff *sending_offs,
                               size_t sending_off_count,
                               ImportedPlayer **out_players,
                               size_t *out_count,
                               char *error, size_t error_size)
{
    JsonSpan array;
    if (!json_object_get(team, "players", &array)) {
        set_error(error, error_size,
                  "The match page does not contain the player lists.");
        return false;
    }
    JsonArrayIter iter;
    if (!json_array_begin(array, &iter)) {
        set_error(error, error_size,
                  "The match page has an invalid player list.");
        return false;
    }

    ImportedPlayer *players = NULL;
    size_t count = 0;
    size_t capacity = 0;
    JsonSpan item;
    while (json_array_next(&iter, &item)) {
        if (item.start >= item.end || *item.start != '{') {
            set_error(error, error_size,
                      "The match page has an invalid player record.");
            goto fail;
        }

        JsonSpan name_span;
        char full_name[512];
        if (!json_object_get(item, "name", &name_span) ||
            !json_decode_string(name_span, full_name, sizeof(full_name))) {
            set_error(error, error_size,
                      "The match page has a player without a name.");
            goto fail;
        }

        bool first_eleven = true;
        JsonSpan flag;
        if (json_object_get(item, "isFirstEleven", &flag)) {
            if (!json_parse_bool(flag, &first_eleven)) {
                set_error(error, error_size,
                          "The match page has an invalid player flag.");
                goto fail;
            }
        } else if (json_object_get(item, "position", &flag)) {
            char position[32];
            if (json_decode_string(flag, position, sizeof(position))) {
                first_eleven = !util_ascii_iequal(position, "Sub");
            }
        }

        int source_id = 0;
        JsonSpan number;
        if (json_object_get(item, "playerId", &number)) {
            int value = 0;
            if (json_parse_int(number, &value) && value > 0) {
                source_id = value;
            }
        }

        bool has_subbed_in = false;
        bool has_subbed_out = false;
        int subbed_in = 0;
        int subbed_out = 0;
        if (json_object_get(item, "subbedInExpandedMinute", &number)) {
            if (!json_parse_int(number, &subbed_in)) {
                set_error(error, error_size,
                          "The match page has an invalid substitution time.");
                goto fail;
            }
            has_subbed_in = true;
        }
        if (json_object_get(item, "subbedOutExpandedMinute", &number)) {
            if (!json_parse_int(number, &subbed_out)) {
                set_error(error, error_size,
                          "The match page has an invalid substitution time.");
                goto fail;
            }
            has_subbed_out = true;
        }

        int in_minute = 0;
        int out_minute = 0;
        if (has_subbed_in && subbed_in > 0) {
            in_minute = match_minute_from_expanded(subbed_in, timing);
        }
        if (has_subbed_out && subbed_out > 0) {
            out_minute = match_minute_from_expanded(subbed_out, timing);
        }
        int off_minute = 0;
        if (source_id > 0) {
            off_minute = sending_off_minute(sending_offs, sending_off_count,
                                            source_id);
        }
        if (off_minute > timing->match_length) {
            off_minute = timing->match_length;
        }

        int minutes = 0;
        if (in_minute > 0) {
            int end_minute = timing->match_length;
            if (out_minute > in_minute) {
                end_minute = out_minute;
            }
            if (off_minute > 0 && off_minute < end_minute) {
                end_minute = off_minute;
            }
            minutes = end_minute - in_minute;
        } else if (out_minute > 0) {
            minutes = out_minute;
            if (off_minute > 0 && off_minute < minutes) {
                minutes = off_minute;
            }
        } else if (first_eleven) {
            minutes = timing->match_length;
            if (off_minute > 0 && off_minute < minutes) {
                minutes = off_minute;
            }
        } else {
            continue;
        }
        if (minutes < 1) {
            minutes = 1;
        }
        if (minutes > 120) {
            minutes = 120;
        }

        if (count >= IMPORT_MAX_PLAYERS) {
            set_error(error, error_size,
                      "The match page contains too many players.");
            goto fail;
        }
        if (!grow_imported_players(&players, &capacity, count + 1)) {
            set_error(error, error_size,
                      "Not enough memory to read the player list.");
            goto fail;
        }
        ImportedPlayer *player = &players[count];
        memset(player, 0, sizeof(*player));
        if (!split_player_name(full_name, player, error, error_size)) {
            goto fail;
        }
        player->minutes = minutes;
        player->sent_off = off_minute;
        ++count;
    }

    if (count == 0) {
        set_error(error, error_size,
                  "The match page does not list any players who appeared.");
        goto fail;
    }
    *out_players = players;
    *out_count = count;
    return true;

fail:
    free(players);
    return false;
}

static bool parse_team(JsonSpan root, const char *key,
                       const MatchTiming *timing,
                       const SendingOff *sending_offs,
                       size_t sending_off_count,
                       char *team_name, size_t team_name_size,
                       ImportedPlayer **players, size_t *player_count,
                       char *error, size_t error_size)
{
    JsonSpan team;
    if (!json_object_get(root, key, &team) ||
        team.start >= team.end || *team.start != '{') {
        set_error(error, error_size,
                  "The match page does not contain the %s team.",
                  key);
        return false;
    }
    JsonSpan name_span;
    if (!json_object_get(team, "name", &name_span)) {
        set_error(error, error_size,
                  "The match page does not contain the %s team name.", key);
        return false;
    }
    if (!decode_trimmed_text(name_span, TEAM_NAME_MAX, team_name,
                             team_name_size, error, error_size,
                             "team name")) {
        return false;
    }
    return parse_team_players(team, timing, sending_offs, sending_off_count,
                              players, player_count, error, error_size);
}

void imported_match_free(ImportedMatch *match)
{
    if (match == NULL) {
        return;
    }
    free(match->home_players);
    free(match->away_players);
    memset(match, 0, sizeof(*match));
}

bool imported_match_parse_html(const char *html, size_t length,
                               ImportedMatch *out,
                               char *error, size_t error_size)
{
    if (out == NULL) {
        set_error(error, error_size, "Invalid import destination.");
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (html == NULL || length == 0) {
        set_error(error, error_size,
                  "The match page was empty.");
        return false;
    }
    if (length > IMPORT_HTML_MAX) {
        set_error(error, error_size,
                  "The match page is too large to import.");
        return false;
    }
    if (!util_utf8_valid_n(html, length)) {
        set_error(error, error_size,
                  "The match page is not valid UTF-8 text.");
        return false;
    }

    JsonSpan root;
    if (!find_match_centre_data(html, length, &root)) {
        set_error(error, error_size,
                  "The page does not contain match data. Use the WhoScored "
                  "page source (Ctrl+U), not the rendered page.");
        return false;
    }

    if (!parse_fixture_date(root, out->date, error, error_size)) {
        return false;
    }
    JsonSpan home;
    JsonSpan away;
    if (!json_object_get(root, "home", &home) ||
        home.start >= home.end || *home.start != '{' ||
        !json_object_get(root, "away", &away) ||
        away.start >= away.end || *away.start != '{') {
        set_error(error, error_size,
                  "The match page does not contain both teams.");
        return false;
    }
    if (!parse_fixture_score(root, home, away, &out->home_goals,
                             &out->away_goals, error, error_size)) {
        return false;
    }

    bool extra_time = false;
    const char *extra_keys[2] = { "etScore", "pkScore" };
    for (int i = 0; i < 2; ++i) {
        JsonSpan span;
        if (!json_object_get(root, extra_keys[i], &span) ||
            span.start >= span.end || *span.start != '"') {
            continue;
        }
        char text[64];
        if (json_decode_string(span, text, sizeof(text)) &&
            text[0] != '\0') {
            extra_time = true;
        }
    }
    MatchTiming timing;
    resolve_match_timing(root, extra_time, &timing);
    out->length = timing.match_length;
    SendingOff sending_offs[IMPORT_MAX_SENDING_OFFS];
    size_t sending_off_count = 0;
    collect_sending_offs(root, sending_offs, &sending_off_count,
                         sizeof(sending_offs) / sizeof(sending_offs[0]));

    if (!parse_team(root, "home", &timing, sending_offs, sending_off_count,
                    out->home_team, sizeof(out->home_team),
                    &out->home_players, &out->home_player_count,
                    error, error_size)) {
        imported_match_free(out);
        return false;
    }
    if (!parse_team(root, "away", &timing, sending_offs, sending_off_count,
                    out->away_team, sizeof(out->away_team),
                    &out->away_players, &out->away_player_count,
                    error, error_size)) {
        imported_match_free(out);
        return false;
    }
    char header_home[TEAM_NAME_MAX + 1];
    char header_away[TEAM_NAME_MAX + 1];
    if (find_match_header_names(html, length, header_home,
                                sizeof(header_home), header_away,
                                sizeof(header_away))) {
        /*
         * The header usually carries the full display name while
         * matchCentreData may carry an abbreviation. Keep the fuller name.
         */
        if (strlen(header_home) >= strlen(out->home_team)) {
            util_str_copy(out->home_team, sizeof(out->home_team),
                          header_home);
        }
        if (strlen(header_away) >= strlen(out->away_team)) {
            util_str_copy(out->away_team, sizeof(out->away_team),
                          header_away);
        }
    }
    if (util_ascii_iequal(out->home_team, out->away_team)) {
        set_error(error, error_size,
                  "The match page lists the same club for both teams.");
        imported_match_free(out);
        return false;
    }
    return true;
}
