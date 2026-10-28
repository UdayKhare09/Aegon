#ifndef AEGON_C_TEST_CHECK_H
#define AEGON_C_TEST_CHECK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s:%d: assertion failed: %s\n", __FILE__, __LINE__, #cond); \
        exit(1); \
    } \
} while (0)

#define CHECK_STR_EQ(a, b) do { \
    const char* _a = (a); \
    const char* _b = (b); \
    if (!_a || !_b || strcmp(_a, _b) != 0) { \
        fprintf(stderr, "FAIL: %s:%d: strings not equal:\n  expected: \"%s\"\n  actual:   \"%s\"\n", \
                __FILE__, __LINE__, _b ? _b : "(null)", _a ? _a : "(null)"); \
        exit(1); \
    } \
} while (0)

#define CHECK_INT_EQ(a, b) do { \
    long long _a = (long long)(a); \
    long long _b = (long long)(b); \
    if (_a != _b) { \
        fprintf(stderr, "FAIL: %s:%d: ints not equal: %lld != %lld\n", __FILE__, __LINE__, _a, _b); \
        exit(1); \
    } \
} while (0)

#endif /* AEGON_C_TEST_CHECK_H */

