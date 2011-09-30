#ifndef _TEST_HELPER_H
#define _TEST_HELPER_H 1

#include <stdio.h>
#include <string.h>
#include <assert.h>

#define assert_equal(a, b, type, fmt) do {       \
    type a_ = (a);                               \
    type b_ = (b);                               \
    if (a_ != b_) {                              \
        fprintf(stderr,                          \
            "%s:%d: " fmt " != " fmt "\n",       \
            __FILE__, __LINE__, a_, b_);         \
        assert(0);                               \
    }                                            \
} while(0)

#define assert_equal_int(a,b) do {               \
    assert_equal(a, b, int, "%d");               \
} while(0)

#define assert_equal_size_t(a,b) do {            \
    assert_equal(a, b, size_t, "%lu");           \
} while(0)

/* Use long long to avoid compiler warnings about the printf format. */
#define assert_equal_int64_t(a,b) do {           \
    assert_equal(a, b, long long, "%lld");       \
} while(0)

#define assert_equal_double(a,b) do {            \
    assert_equal(a, b, double, "%f");            \
} while(0)

#define assert_equal_string(a,b) do {            \
    if (strcmp(a, b)) {                          \
        fprintf(stderr,                          \
            "%s:%d: %s != %s\n",                 \
            __FILE__, __LINE__, a, b);           \
        assert(0);                               \
    }                                            \
} while(0)

#endif
