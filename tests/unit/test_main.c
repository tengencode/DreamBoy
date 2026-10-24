#include <stdio.h>
#include <string.h>

#include "test.h"

#define MAX_TESTS 1024

static struct {
    const char *name;
    test_fn fn;
} tests[MAX_TESTS];
static int test_count;
static int current_failed;

void test_register(const char *name, test_fn fn) {
    if (test_count < MAX_TESTS) {
        tests[test_count].name = name;
        tests[test_count].fn = fn;
        test_count++;
    }
}

void test_fail(void) {
    current_failed = 1;
}

int main(int argc, char **argv) {
    const char *filter = argc > 1 ? argv[1] : NULL;
    int run = 0, failed = 0;
    for (int i = 0; i < test_count; i++) {
        if (filter && !strstr(tests[i].name, filter)) continue;
        current_failed = 0;
        tests[i].fn();
        run++;
        if (current_failed) {
            failed++;
            printf("[ FAIL ] %s\n", tests[i].name);
        } else {
            printf("[ PASS ] %s\n", tests[i].name);
        }
    }
    printf("%d tests, %d failed\n", run, failed);
    return failed ? 1 : 0;
}
