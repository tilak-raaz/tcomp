/**
 * @file test.h
 * @brief A deliberately tiny unit-test framework. No dependencies.
 *
 * Usage:
 *   TEST(test_something) { CHECK(1 + 1 == 2); }
 *   int main(void) { RUN(test_something); return test_summary(); }
 *
 * A failing CHECK prints file:line and the expression, marks the test as
 * failed, and keeps going so one run shows every failure.
 *
 * Include this header from exactly one .c file (it defines static state).
 */
#ifndef TCOMP_TEST_H
#define TCOMP_TEST_H

#include <stdio.h>

static int test_current_failed;
static int test_total;
static int test_failed;

#define TEST(name) static void name(void)

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "  %s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);             \
            test_current_failed = 1;                                                               \
        }                                                                                          \
    } while (0)

#define RUN(name)                                                                                  \
    do {                                                                                           \
        test_current_failed = 0;                                                                   \
        name();                                                                                    \
        test_total++;                                                                              \
        if (test_current_failed) {                                                                 \
            test_failed++;                                                                         \
            fprintf(stderr, "FAIL %s\n", #name);                                                   \
        } else {                                                                                   \
            printf("ok   %s\n", #name);                                                            \
        }                                                                                          \
    } while (0)

static inline int test_summary(void) {
    printf("\n%d/%d tests passed\n", test_total - test_failed, test_total);
    return test_failed == 0 ? 0 : 1;
}

#endif /* TCOMP_TEST_H */
