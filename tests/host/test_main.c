/* test_main.c - host unit test runner. */
#include "test.h"

struct test_case *test_list_head = NULL;
int test_current_failed = 0;
long test_assertions = 0;

void test_register(struct test_case *tc) {
    /* Prepend; order does not matter, every test is independent. */
    tc->next = test_list_head;
    test_list_head = tc;
}

int main(void) {
    int total = 0;
    int failed = 0;

    for (struct test_case *tc = test_list_head; tc != NULL; tc = tc->next) {
        test_current_failed = 0;
        printf("  RUN  %s\n", tc->name);
        tc->fn();
        if (test_current_failed) {
            failed++;
        }
        total++;
    }

    printf("host tests: %d/%d passed, %ld assertions\n", total - failed, total, test_assertions);
    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
