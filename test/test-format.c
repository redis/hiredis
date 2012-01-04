#include "../fmacros.h"

/* misc */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* local */
#include "../format.h"
#include "test-helper.h"

#define SETUP()                                                                \
    char *cmd;                                                                 \
    int len;

TEST(format_without_interpolation) {
    SETUP();

    len = redis_format_command(&cmd, "SET foo bar");
    assert(strncmp(cmd, "*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n", len) == 0 &&
        len == 4+4+(3+2)+4+(3+2)+4+(3+2));
    free(cmd);
}

TEST(format_with_string_interpolation) {
    SETUP();

    /* Regular */
    len = redis_format_command(&cmd, "SET %s %s", "foo", "bar");
    assert(strncmp(cmd, "*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n", len) == 0 &&
        len == 4+4+(3+2)+4+(3+2)+4+(3+2));
    free(cmd);

    /* With empty string */
    len = redis_format_command(&cmd, "SET %s %s", "foo", "");
    assert(strncmp(cmd, "*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$0\r\n\r\n", len) == 0 &&
        len == 4+4+(3+2)+4+(3+2)+4+(0+2));
    free(cmd);

    /* Empty string between non-empty args */
    len = redis_format_command(&cmd, "SET %s %s", "", "bar");
    assert(strncmp(cmd, "*3\r\n$3\r\nSET\r\n$0\r\n\r\n$3\r\nbar\r\n", len) == 0 &&
        len == 4+4+(3+2)+4+(0+2)+4+(3+2));
    free(cmd);
}

TEST(format_with_binary_interpolation) {
    SETUP();

    /* Regular */
    len = redis_format_command(&cmd, "SET %b %b", "f\0o", 3, "b\0r", 3);
    assert(strncmp(cmd, "*3\r\n$3\r\nSET\r\n$3\r\nf\0o\r\n$3\r\nb\0r\r\n", len) == 0 &&
        len == 4+4+(3+2)+4+(3+2)+4+(3+2));
    free(cmd);

    /* With empty string */
    len = redis_format_command(&cmd, "SET %b %b", "f\0o", 3, "", 0);
    assert(strncmp(cmd, "*3\r\n$3\r\nSET\r\n$3\r\nf\0o\r\n$0\r\n\r\n", len) == 0 &&
        len == 4+4+(3+2)+4+(3+2)+4+(0+2));
    free(cmd);

    /* Empty string between non-empty args */
    len = redis_format_command(&cmd, "SET %b %b", "", 0, "b\0r", 3);
    assert(strncmp(cmd, "*3\r\n$3\r\nSET\r\n$0\r\n\r\n$3\r\nb\0r\r\n", len) == 0 &&
        len == 4+4+(3+2)+4+(0+2)+4+(3+2));
    free(cmd);
}

TEST(format_with_literal_percent) {
    SETUP();

    len = redis_format_command(&cmd,"SET %% %%");
    assert(strncmp(cmd,"*3\r\n$3\r\nSET\r\n$1\r\n%\r\n$1\r\n%\r\n",len) == 0 &&
        len == 4+4+(3+2)+4+(1+2)+4+(1+2));
    free(cmd);
}

TEST(format_with_printf_format) {
    SETUP();

    /* Vararg width depends on the type. These tests make sure that the
     * width is correctly determined using the format and subsequent varargs
     * can correctly be interpolated. */
#define INTEGER_WIDTH_TEST(fmt, type) do {                                                \
    type value = 123;                                                                     \
    len = redis_format_command(&cmd,"key:%08" fmt " str:%s", value, "hello");               \
    assert(strncmp(cmd,"*2\r\n$12\r\nkey:00000123\r\n$9\r\nstr:hello\r\n",len) == 0 && \
        len == 4+5+(12+2)+4+(9+2));                                                       \
    free(cmd);                                                                            \
} while(0)

#define FLOAT_WIDTH_TEST(type) do {                                                       \
    type value = 123.0;                                                                   \
    len = redis_format_command(&cmd,"key:%08.3f str:%s", value, "hello");                   \
    assert(strncmp(cmd,"*2\r\n$12\r\nkey:0123.000\r\n$9\r\nstr:hello\r\n",len) == 0 && \
        len == 4+5+(12+2)+4+(9+2));                                                       \
    free(cmd);                                                                            \
} while(0)

    INTEGER_WIDTH_TEST("d", int);
    INTEGER_WIDTH_TEST("hhd", char);
    INTEGER_WIDTH_TEST("hd", short);
    INTEGER_WIDTH_TEST("ld", long);
    INTEGER_WIDTH_TEST("lld", long long);
    INTEGER_WIDTH_TEST("u", unsigned int);
    INTEGER_WIDTH_TEST("hhu", unsigned char);
    INTEGER_WIDTH_TEST("hu", unsigned short);
    INTEGER_WIDTH_TEST("lu", unsigned long);
    INTEGER_WIDTH_TEST("llu", unsigned long long);
    FLOAT_WIDTH_TEST(float);
    FLOAT_WIDTH_TEST(double);
}

TEST(format_with_invalid_printf_format) {
    SETUP();

    len = redis_format_command(&cmd,"key:%08p %b",1234,"foo",3);
    assert(len == -1);
}

TEST(format_argv) {
    SETUP();

    int argc = 3;
    const char *argv[3] = { "SET", "foo\0xxx", "bar" };
    size_t lens[3] = { 3, 7, 3 };

    /* Without argument length */
    len = redis_format_command_argv(&cmd,argc,argv,NULL);
    assert(strncmp(cmd,"*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n",len) == 0 &&
        len == 4+4+(3+2)+4+(3+2)+4+(3+2));
    free(cmd);

    /* With argument length */
    len = redis_format_command_argv(&cmd,argc,argv,lens);
    assert(strncmp(cmd,"*3\r\n$3\r\nSET\r\n$7\r\nfoo\0xxx\r\n$3\r\nbar\r\n",len) == 0 &&
        len == 4+4+(3+2)+4+(7+2)+4+(3+2));
    free(cmd);
}

int main(void) {
    test_format_without_interpolation();
    test_format_with_string_interpolation();
    test_format_with_binary_interpolation();
    test_format_with_literal_percent();
    test_format_with_printf_format();
    test_format_with_invalid_printf_format();
    test_format_argv();
}
