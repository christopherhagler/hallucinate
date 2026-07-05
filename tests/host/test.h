/*
 * test.h - minimal host-side unit test framework.
 *
 * Tests register themselves via the TEST() macro constructor; the runner in
 * test_main.c executes them all and reports a summary. Assertion failures
 * mark the current test failed but keep running the remaining tests so a
 * whole run's worth of information comes out of one execution.
 */
#ifndef HL_TEST_H
#define HL_TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct test_case {
    const char *name;
    void (*fn)(void);
    struct test_case *next;
};

extern struct test_case *test_list_head;
extern int test_current_failed;
extern long test_assertions;

void test_register(struct test_case *tc);

#define TEST(name)                                                                                 \
    static void test_fn_##name(void);                                                              \
    static struct test_case test_case_##name = {#name, test_fn_##name, NULL};                      \
    __attribute__((constructor)) static void test_reg_##name(void) {                               \
        test_register(&test_case_##name);                                                          \
    }                                                                                              \
    static void test_fn_##name(void)

#define TEST_FAIL(fmt, ...)                                                                        \
    do {                                                                                           \
        fprintf(stderr, "    FAIL %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__);           \
        test_current_failed = 1;                                                                   \
    } while (0)

#define ASSERT_TRUE(cond)                                                                          \
    do {                                                                                           \
        test_assertions++;                                                                         \
        if (!(cond)) {                                                                             \
            TEST_FAIL("expected true: %s", #cond);                                                 \
        }                                                                                          \
    } while (0)

#define ASSERT_EQ_INT(expected, actual)                                                            \
    do {                                                                                           \
        test_assertions++;                                                                         \
        long long e_ = (long long)(expected);                                                      \
        long long a_ = (long long)(actual);                                                        \
        if (e_ != a_) {                                                                            \
            TEST_FAIL("%s == %s: expected %lld, got %lld", #expected, #actual, e_, a_);            \
        }                                                                                          \
    } while (0)

#define ASSERT_EQ_STR(expected, actual)                                                            \
    do {                                                                                           \
        test_assertions++;                                                                         \
        const char *e_ = (expected);                                                               \
        const char *a_ = (actual);                                                                 \
        if (strcmp(e_, a_) != 0) {                                                                 \
            TEST_FAIL("%s: expected \"%s\", got \"%s\"", #actual, e_, a_);                         \
        }                                                                                          \
    } while (0)

#endif /* HL_TEST_H */
