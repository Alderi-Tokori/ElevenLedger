#ifndef ELEVENLEDGER_TEST_FRAMEWORK_H
#define ELEVENLEDGER_TEST_FRAMEWORK_H

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef bool (*TestCaseFunction)(void);

typedef struct {
    const char *name;
    TestCaseFunction function;
} TestCase;

#define TEST_CASE(function_name) { #function_name, function_name }

#define CHECK(expression)                                                    \
    do {                                                                     \
        if (!(expression)) {                                                 \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__,       \
                    #expression);                                            \
            return false;                                                    \
        }                                                                    \
    } while (0)

#define CHECK_NEAR(actual, expected, epsilon)                                \
    do {                                                                     \
        double check_actual_ = (double)(actual);                             \
        double check_expected_ = (double)(expected);                         \
        double check_epsilon_ = (double)(epsilon);                           \
        if (!(fabs(check_actual_ - check_expected_) <= check_epsilon_)) {   \
            fprintf(stderr,                                                  \
                    "  FAIL %s:%d: expected %.6f, got %.6f\n",              \
                    __FILE__, __LINE__, check_expected_, check_actual_);     \
            return false;                                                    \
        }                                                                    \
    } while (0)

#define CHECK_STRING(actual, expected)                                       \
    do {                                                                     \
        const char *check_actual_ = (actual);                                \
        const char *check_expected_ = (expected);                            \
        if (check_actual_ == NULL || check_expected_ == NULL ||              \
            strcmp(check_actual_, check_expected_) != 0) {                   \
            fprintf(stderr, "  FAIL %s:%d: expected \"%s\", got \"%s\"\n",  \
                    __FILE__, __LINE__,                                      \
                    check_expected_ == NULL ? "(null)" : check_expected_,    \
                    check_actual_ == NULL ? "(null)" : check_actual_);       \
            return false;                                                    \
        }                                                                    \
    } while (0)

#endif
