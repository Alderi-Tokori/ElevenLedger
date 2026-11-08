#include "test_framework.h"

#include "core/util.h"

#include <string.h>

static bool test_utf8_validation(void)
{
    CHECK(util_utf8_valid(""));
    CHECK(util_utf8_valid("Plain ASCII"));
    CHECK(util_utf8_valid("Étoile"));
    CHECK(util_utf8_valid("Müller"));
    CHECK(util_utf8_valid("李娜"));
    CHECK(util_utf8_valid("\xF0\x9F\x9A\x80")); /* U+1F680 */

    CHECK(!util_utf8_valid(NULL));
    CHECK(!util_utf8_valid("\x80"));
    CHECK(!util_utf8_valid("\xC0\xAF"));       /* overlong */
    CHECK(!util_utf8_valid("\xE0\x80\x80"));   /* overlong */
    CHECK(!util_utf8_valid("\xED\xA0\x80"));   /* surrogate */
    CHECK(!util_utf8_valid("\xF4\x90\x80\x80")); /* > U+10FFFF */
    CHECK(!util_utf8_valid("\xE2\x82"));       /* truncated */
    return true;
}

static bool test_text_validation(void)
{
    CHECK(util_text_valid("Arsenal", 63, false));
    CHECK(util_text_valid("", 63, true));
    CHECK(!util_text_valid("", 63, false));
    CHECK(!util_text_valid(" leading", 63, true));
    CHECK(!util_text_valid("trailing ", 63, true));
    CHECK(!util_text_valid("line\nbreak", 63, true));
    CHECK(!util_text_valid("tab\tcharacter", 63, true));
    CHECK(!util_text_valid("0123456789", 9, true));
    CHECK(util_text_valid("0123456789", 10, true));
    CHECK(!util_text_valid(NULL, 63, true));
    return true;
}

static bool test_trim(void)
{
    char text[] = "  \t Hello world \r\n ";
    char *trimmed = util_trim(text);
    CHECK_STRING(trimmed, "Hello world");
    char blank[] = "   \t";
    CHECK_STRING(util_trim(blank), "");
    return true;
}

static bool test_ascii_case_equal(void)
{
    CHECK(util_ascii_iequal("Arsenal", "arsenal"));
    CHECK(util_ascii_iequal("A", "a"));
    CHECK(!util_ascii_iequal("Arsenal", "Arsenals"));
    CHECK(!util_ascii_iequal("", "a"));
    CHECK(util_ascii_iequal("", ""));
    CHECK(!util_ascii_iequal(NULL, "a"));
    return true;
}

static bool test_number_parsing(void)
{
    int integer = 0;
    CHECK(util_parse_int("42", 1, 99, &integer));
    CHECK(integer == 42);
    CHECK(!util_parse_int("0", 1, 99, &integer));
    CHECK(!util_parse_int("100", 1, 99, &integer));
    CHECK(!util_parse_int("4x", 1, 99, &integer));
    CHECK(!util_parse_int("", 1, 99, &integer));

    double number = 0.0;
    CHECK(util_parse_double("7.25", 1.0, 10.0, &number));
    CHECK_NEAR(number, 7.25, 0.0001);
    CHECK(!util_parse_double("nan", 1.0, 10.0, &number));
    CHECK(!util_parse_double("inf", 1.0, 10.0, &number));
    CHECK(!util_parse_double("10.01", 1.0, 10.0, &number));
    CHECK(!util_parse_double("1.0x", 1.0, 10.0, &number));
    return true;
}

static bool test_dates(void)
{
    CHECK(util_date_valid("2026-11-08"));
    CHECK(util_date_valid("2024-02-29"));
    CHECK(util_date_valid("2000-02-29"));
    CHECK(!util_date_valid("1900-02-29"));
    CHECK(!util_date_valid("2023-02-29"));
    CHECK(!util_date_valid("2026-13-01"));
    CHECK(!util_date_valid("2026-00-01"));
    CHECK(!util_date_valid("2026-01-00"));
    CHECK(!util_date_valid("2026-04-31"));
    CHECK(!util_date_valid("26-11-08"));
    CHECK(!util_date_valid("2026/11/08"));
    CHECK(!util_date_valid("1899-12-31"));
    CHECK(!util_date_valid("2201-01-01"));
    CHECK(!util_date_valid(""));

    char today[11];
    util_date_today(today);
    CHECK(util_date_valid(today));
    return true;
}

static bool test_crc32(void)
{
    CHECK(util_crc32("123456789", 9) == 0xCBF43926u);
    CHECK(util_crc32("", 0) == 0x00000000u);

    uint32_t state = util_crc32_begin();
    state = util_crc32_extend(state, "1234", 4);
    state = util_crc32_extend(state, "56789", 5);
    CHECK(util_crc32_finish(state) == 0xCBF43926u);
    return true;
}

static bool test_string_copy(void)
{
    char buffer[8];
    util_str_copy(buffer, sizeof(buffer), "abcdef");
    CHECK_STRING(buffer, "abcdef");
    util_str_copy(buffer, sizeof(buffer), "abcdefghi");
    CHECK_STRING(buffer, "abcdefg");
    util_str_copy(buffer, sizeof(buffer), NULL);
    CHECK_STRING(buffer, "");
    util_str_copy(NULL, 0, "ignored");
    return true;
}

const TestCase g_util_tests[] = {
    TEST_CASE(test_utf8_validation),
    TEST_CASE(test_text_validation),
    TEST_CASE(test_trim),
    TEST_CASE(test_ascii_case_equal),
    TEST_CASE(test_number_parsing),
    TEST_CASE(test_dates),
    TEST_CASE(test_crc32),
    TEST_CASE(test_string_copy),
};

const size_t g_util_test_count =
    sizeof(g_util_tests) / sizeof(g_util_tests[0]);
