/* Unit test runner: calls every suite and reports the total.
 * Adding a module's tests = write tests/unit/test_<module>.c, declare its
 * suite in test.h, and call it below. */
#include "test.h"

int test_current_failed;
int test_total;
int test_failed;

int main(void) {
    printf("== container\n");
    suite_container();
    printf("== bitio\n");
    suite_bitio();

    printf("\n%d/%d tests passed\n", test_total - test_failed, test_total);
    return test_failed == 0 ? 0 : 1;
}
