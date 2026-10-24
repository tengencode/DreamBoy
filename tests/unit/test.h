#ifndef TEST_H
#define TEST_H

#include <stdio.h>

typedef void (*test_fn)(void);

void test_register(const char *name, test_fn fn);
void test_fail(void);

#define TEST(name)                                                   \
    static void name(void);                                          \
    __attribute__((constructor)) static void name##_register(void) { \
        test_register(#name, name);                                  \
    }                                                                \
    static void name(void)

#define CHECK(cond)                                                             \
    do {                                                                        \
        if (!(cond)) {                                                          \
            fprintf(stderr, "  %s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            test_fail();                                                        \
        }                                                                       \
    } while (0)

#define CHECK_EQ(actual, expected)                                               \
    do {                                                                         \
        unsigned long long a_ = (unsigned long long)(actual);                    \
        unsigned long long e_ = (unsigned long long)(expected);                  \
        if (a_ != e_) {                                                          \
            fprintf(stderr, "  %s:%d: CHECK_EQ(%s, %s): 0x%llx != 0x%llx\n",     \
                    __FILE__, __LINE__, #actual, #expected, a_, e_);             \
            test_fail();                                                         \
        }                                                                        \
    } while (0)

#endif
