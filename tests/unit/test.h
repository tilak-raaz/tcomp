/**
 * @file test.h
 * @brief A deliberately tiny unit-test framework. No dependencies.
 *
 * Each module gets its own file, tests/unit/test_<module>.c, containing
 * static TEST functions and one public suite function that RUNs them:
 *
 *   TEST(test_something) { CHECK(1 + 1 == 2); }
 *   void suite_example(void) { RUN(test_something); }
 *
 * Declare the suite below and call it from main() in tests/unit/main.c.
 *
 * A failing CHECK prints file:line and the expression, marks the test as
 * failed, and keeps going so one run shows every failure.
 */
#ifndef TCOMP_TEST_H
#define TCOMP_TEST_H

#include <stdio.h>

/* Counters shared by all suites; defined in main.c. */
extern int test_current_failed;
extern int test_total;
extern int test_failed;

/* One entry per tests/unit/test_<module>.c file. */
void suite_container(void);
void suite_bitio(void);
void suite_huffman(void);

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

#endif /* TCOMP_TEST_H */
