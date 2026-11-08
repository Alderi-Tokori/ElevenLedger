#include "test_framework.h"

#include <stddef.h>
#include <stdio.h>

extern const TestCase g_util_tests[];
extern const size_t g_util_test_count;
extern const TestCase g_rating_tests[];
extern const size_t g_rating_test_count;
extern const TestCase g_database_tests[];
extern const size_t g_database_test_count;
extern const TestCase g_storage_tests[];
extern const size_t g_storage_test_count;

static bool run_group(const char *name, const TestCase *tests, size_t count,
                      size_t *passed, size_t *failed)
{
    printf("\n[%s]\n", name);
    for (size_t i = 0; i < count; ++i) {
        printf("  %-52s ... ", tests[i].name);
        fflush(stdout);
        bool ok = tests[i].function();
        if (ok) {
            puts("ok");
            (*passed)++;
        } else {
            puts("FAILED");
            (*failed)++;
        }
    }
    return *failed == 0;
}

int main(void)
{
    size_t passed = 0;
    size_t failed = 0;
    printf("PitchRate automated test suite\n");
    printf("==============================\n");
    run_group("util", g_util_tests, g_util_test_count, &passed, &failed);
    run_group("rating", g_rating_tests, g_rating_test_count, &passed, &failed);
    run_group("database", g_database_tests, g_database_test_count,
              &passed, &failed);
    run_group("storage", g_storage_tests, g_storage_test_count,
              &passed, &failed);

    printf("\n%zu passed, %zu failed.\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
