#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "parser.h"

#define assert_equal_size_t(a,b) do {            \
    __typeof__ (a) a_ = (a);                     \
    __typeof__ (b) b_ = (b);                     \
    if (a_ != b_) {                              \
        fprintf(stderr, "%s:%d: %lu != %lu\n",   \
            __FILE__, __LINE__, a_, b_);         \
        assert(0);                               \
    }                                            \
} while(0)

typedef struct log_entry_s log_entry_t;

struct log_entry_s {
    redis_protocol_t obj;
    int type;

    /* string_t specifics */
    char string_buf[1024];

    /* array_t specifics */
    size_t array_len;

    /* integer_t specifics */
    int64_t integer_value;
};

#define CB_LOG_SIZE 10
static log_entry_t cb_log[CB_LOG_SIZE];
static int cb_log_idx = 0;

static void reset_cb_log(void) {
    memset(cb_log, 0xff, sizeof(cb_log));
    cb_log_idx = 0;
}

static log_entry_t *dup_cb_log(void) {
    log_entry_t *log = malloc(sizeof(cb_log));
    memcpy(log, cb_log, sizeof(cb_log));
    return log;
}

int on_string(redis_parser_t *parser, redis_protocol_t *p, const char *buf, size_t len) {
    log_entry_t *log = (log_entry_t*)p->data;

    /* Use new entry when this is the first call */
    if (log == NULL) {
        log = p->data = &cb_log[cb_log_idx++];
        log->obj = *p;
        log->type = p->type;
        log->string_buf[0] = '\0';
    }

    /* Type should never change when called multiple times */
    assert(log->type == p->type);

    /* Cursor should equal current string length */
    assert(p->cursor == strlen(log->string_buf));

    /* Append string data */
    strncat(log->string_buf, buf, len);

    return 0;
}

int on_array(redis_parser_t *parser, redis_protocol_t *p, size_t len) {
    log_entry_t *log = (log_entry_t*)p->data;

    /* Should only be called once */
    assert(log == NULL);

    /* Use new entry */
    log = p->data = &cb_log[cb_log_idx++];
    log->obj = *p;
    log->type = p->type;
    log->array_len = len;

    return 0;
}

int on_integer(redis_parser_t *parser, redis_protocol_t *p, int64_t value) {
    log_entry_t *log = (log_entry_t*)p->data;

    /* Should only be called once */
    assert(log == NULL);

    /* Use new entry */
    log = p->data = &cb_log[cb_log_idx++];
    log->obj = *p;
    log->type = p->type;
    log->integer_value = value;

    return 0;
}

int on_nil(redis_parser_t *parser, redis_protocol_t *p) {
    log_entry_t *log = (log_entry_t*)p->data;

    /* Should only be called once */
    assert(log == NULL);

    /* Use new entry */
    log = p->data = &cb_log[cb_log_idx++];
    log->obj = *p;
    log->type = p->type;

    return 0;
}

static redis_parser_callbacks_t callbacks = {
    &on_string,
    &on_array,
    &on_integer,
    &on_nil
};

#define RESET_PARSER_T(__parser) do {           \
    reset_cb_log();                             \
    redis_parser_init((__parser), &callbacks);  \
} while(0)

void test_char_by_char(redis_protocol_t *_unused, const char *buf, size_t len) {
    log_entry_t *ref;
    redis_parser_t *p;
    redis_protocol_t *res;
    size_t i, j;

    ref = dup_cb_log();
    p = malloc(sizeof(redis_parser_t));

    for (i = 1; i < (len-1); i++) {
        RESET_PARSER_T(p);

        /* Slice 1 */
        assert_equal_size_t(redis_parser_execute(p, &res, buf, i), i);
        assert(NULL == res); /* no result */

        /* Slice 2 */
        assert_equal_size_t(redis_parser_execute(p, &res, buf+i, len-i), len-i);
        assert(NULL != res);

        /* Compare callback log with reference */
        for (j = 0; j < CB_LOG_SIZE; j++) {
            log_entry_t expect = ref[j];
            log_entry_t actual = cb_log[j];

            /* Not interested in the redis_protocol_t data */
            memset(&expect.obj, 0, sizeof(expect.obj));
            memset(&actual.obj, 0, sizeof(actual.obj));
            assert(memcmp(&expect, &actual, sizeof(expect)) == 0);
        }
    }

    free(p);
    free(ref);
}

void test_string(redis_parser_t *p) {
    const char *buf = "$5\r\nhello\r\n";
    size_t len = 11;
    redis_protocol_t *res;

    /* Parse and check resulting protocol_t */
    RESET_PARSER_T(p);
    assert_equal_size_t(redis_parser_execute(p, &res, buf, len), len);
    assert(res != NULL);
    assert(res->type == REDIS_STRING_T);
    assert(res->poff == 0);
    assert(res->plen == 11);
    assert(res->coff == 4);
    assert(res->clen == 5);

    /* Check callbacks */
    assert(cb_log_idx == 1);
    assert(!strncmp(cb_log[0].string_buf, buf+4, 5));

    /* Chunked check */
    test_char_by_char(res, buf, len);
}

void test_empty_string(redis_parser_t *p) {
    const char *buf = "$0\r\n\r\n";
    size_t len = 6;
    redis_protocol_t *res;

    /* Parse and check resulting protocol_t */
    RESET_PARSER_T(p);
    assert_equal_size_t(redis_parser_execute(p, &res, buf, len), len);
    assert(res != NULL);
    assert(res->type == REDIS_STRING_T);
    assert(res->poff == 0);
    assert(res->plen == 6);
    assert(res->coff == 4);
    assert(res->clen == 0);

    /* Check callbacks */
    assert(cb_log_idx == 1);
    assert(!strncmp(cb_log[0].string_buf, buf+4, 0));

    /* Chunked check */
    test_char_by_char(res, buf, len);
}

void test_array(redis_parser_t *p) {
    const char *buf =
        "*2\r\n"
        "$5\r\nhello\r\n"
        "$5\r\nworld\r\n";
    size_t len = 26;
    redis_protocol_t *res;

    /* Parse and check resulting protocol_t */
    RESET_PARSER_T(p);
    assert_equal_size_t(redis_parser_execute(p, &res, buf, len), len);
    assert(res != NULL);
    assert(res->type == REDIS_ARRAY_T);
    assert(res->poff == 0);
    assert(res->plen == 26);
    assert(res->coff == 0);
    assert(res->clen == 0);

    /* Check callbacks */
    assert(cb_log_idx == 3);

    assert(cb_log[0].obj.poff == 0);
    assert(cb_log[0].obj.plen == 4);
    assert(cb_log[0].array_len == 2);

    assert(cb_log[1].obj.poff == 4);
    assert(cb_log[1].obj.plen == 4+5+2);
    assert(cb_log[1].obj.coff == 4+4);
    assert(cb_log[1].obj.clen == 5);
    assert(!strncmp(cb_log[1].string_buf, buf+4+4, 5));

    assert(cb_log[2].obj.poff == 4+11);
    assert(cb_log[2].obj.plen == 4+5+2);
    assert(cb_log[2].obj.coff == 4+11+4);
    assert(cb_log[2].obj.clen == 5);
    assert(!strncmp(cb_log[2].string_buf, buf+4+11+4, 5));

    /* Chunked check */
    test_char_by_char(res, buf, len);
}

void test_empty_array(redis_parser_t *p) {
    const char *buf = "*0\r\n";
    size_t len = 4;
    redis_protocol_t *res;

    /* Parse and check resulting protocol_t */
    RESET_PARSER_T(p);
    assert_equal_size_t(redis_parser_execute(p, &res, buf, len), len);
    assert(res != NULL);
    assert(res->type == REDIS_ARRAY_T);
    assert(res->poff == 0);
    assert(res->plen == 4);

    /* Check callbacks */
    assert(cb_log_idx == 1);
    assert(cb_log[0].array_len == 0);

    /* Chunked check */
    test_char_by_char(res, buf, len);
}

void test_integer(redis_parser_t *p) {
    const char *buf = ":1234\r\n";
    size_t len = 7;
    redis_protocol_t *res;

    /* Parse and check resulting protocol_t */
    RESET_PARSER_T(p);
    assert_equal_size_t(redis_parser_execute(p, &res, buf, len), len);
    assert(res != NULL);
    assert(res->type == REDIS_INTEGER_T);
    assert(res->poff == 0);
    assert(res->plen == 7);
    assert(res->coff == 1);
    assert(res->clen == 4);

    /* Check callbacks */
    assert(cb_log_idx == 1);
    assert(cb_log[0].integer_value == 1234);

    /* Chunked check */
    test_char_by_char(res, buf, len);

    /* Negative sign */
    buf = ":-123\r\n";
    RESET_PARSER_T(p);
    assert(redis_parser_execute(p, &res, buf, strlen(buf)) == strlen(buf));
    assert(res != NULL);
    assert(cb_log_idx == 1 && cb_log[0].integer_value == -123);
    test_char_by_char(res, buf, strlen(buf));

    /* Positive sign */
    buf = ":+123\r\n";
    RESET_PARSER_T(p);
    assert(redis_parser_execute(p, &res, buf, strlen(buf)) == strlen(buf));
    assert(res != NULL);
    assert(cb_log_idx == 1 && cb_log[0].integer_value == 123);
    test_char_by_char(res, buf, strlen(buf));

    /* Zero */
    buf = ":0\r\n";
    RESET_PARSER_T(p);
    assert(redis_parser_execute(p, &res, buf, strlen(buf)) == strlen(buf));
    assert(res != NULL);
    assert(cb_log_idx == 1 && cb_log[0].integer_value == 0);
    test_char_by_char(res, buf, strlen(buf));

    /* Signed zero, positive */
    buf = ":+0\r\n";
    RESET_PARSER_T(p);
    assert(redis_parser_execute(p, &res, buf, strlen(buf)) == 2);
    assert(res == NULL);
    assert(redis_parser_errno(p) == REDIS_PARSER_ERR_INVALID_INT);

    /* Signed zero, negative */
    buf = ":-0\r\n";
    RESET_PARSER_T(p);
    assert(redis_parser_execute(p, &res, buf, strlen(buf)) == 2);
    assert(res == NULL);
    assert(redis_parser_errno(p) == REDIS_PARSER_ERR_INVALID_INT);

    /* Start with 0 */
    buf = ":0123\r\n";
    RESET_PARSER_T(p);
    assert(redis_parser_execute(p, &res, buf, strlen(buf)) == 2);
    assert(res == NULL);
    assert(redis_parser_errno(p) == REDIS_PARSER_ERR_EXPECTED_CR);

    /* Start with non-digit */
    buf = ":x123\r\n";
    RESET_PARSER_T(p);
    assert(redis_parser_execute(p, &res, buf, strlen(buf)) == 1);
    assert(res == NULL);
    assert(redis_parser_errno(p) == REDIS_PARSER_ERR_INVALID_INT);

    /* Non-digit in the middle */
    buf = ":12x3\r\n";
    RESET_PARSER_T(p);
    assert(redis_parser_execute(p, &res, buf, strlen(buf)) == 3);
    assert(res == NULL);
    assert(redis_parser_errno(p) == REDIS_PARSER_ERR_INVALID_INT);

    /* Non-digit at the end */
    buf = ":123x\r\n";
    RESET_PARSER_T(p);
    assert(redis_parser_execute(p, &res, buf, strlen(buf)) == 4);
    assert(res == NULL);
    assert(redis_parser_errno(p) == REDIS_PARSER_ERR_INVALID_INT);

    /* Signed 64-bit maximum */
    buf = ":9223372036854775807\r\n";
    RESET_PARSER_T(p);
    assert(redis_parser_execute(p, &res, buf, strlen(buf)) == strlen(buf));
    assert(res != NULL);
    assert(cb_log_idx == 1 && cb_log[0].integer_value == INT64_MAX);

    /* Signed 64-bit maximum overflow */
    buf = ":9223372036854775808\r\n";
    RESET_PARSER_T(p);
    assert(redis_parser_execute(p, &res, buf, strlen(buf)) == strlen(buf)-3);
    assert(res == NULL);
    assert(redis_parser_errno(p) == REDIS_PARSER_ERR_OVERFLOW);

    /* Signed 64-bit minimum */
    buf = ":-9223372036854775808\r\n";
    RESET_PARSER_T(p);
    assert(redis_parser_execute(p, &res, buf, strlen(buf)) == strlen(buf));
    assert(res != NULL);
    assert(cb_log_idx == 1 && cb_log[0].integer_value == INT64_MIN);

    /* Signed 64-bit minimum overflow (or underflow...) */
    buf = ":-9223372036854775809\r\n";
    RESET_PARSER_T(p);
    assert(redis_parser_execute(p, &res, buf, strlen(buf)) == strlen(buf)-3);
    assert(res == NULL);
    assert(redis_parser_errno(p) == REDIS_PARSER_ERR_OVERFLOW);
}

void test_nil(redis_parser_t *p) {
    const char *buf = "$-1\r\n";
    size_t len = 7;
    redis_protocol_t *res;

    /* Parse and check resulting protocol_t */
    RESET_PARSER_T(p);
    assert_equal_size_t(redis_parser_execute(p, &res, buf, len), len);
    assert(res != NULL);
    assert(res->type == REDIS_INTEGER_T);
    assert(res->poff == 0);
    assert(res->plen == 7);
    assert(res->coff == 1);
    assert(res->clen == 4);

    /* Check callbacks */
    assert(cb_log_idx == 1);
    assert(cb_log[0].integer_value == 1234);

    /* Chunked check */
    test_char_by_char(res, buf, len);
}

void test_status(redis_parser_t *p) {
    const char *buf = "+status\r\n";
    size_t len = 9;
    redis_protocol_t *res;

    /* Parse and check resulting protocol_t */
    RESET_PARSER_T(p);
    assert_equal_size_t(redis_parser_execute(p, &res, buf, len), len);
    assert(res != NULL);
    assert(res->type == REDIS_STATUS_T);
    assert(res->poff == 0);
    assert(res->plen == 9);
    assert(res->coff == 1);
    assert(res->clen == 6);
}

void test_error(redis_parser_t *p) {
    const char *buf = "-error\r\n";
    size_t len = 8;
    redis_protocol_t *res;

    /* Parse and check resulting protocol_t */
    RESET_PARSER_T(p);
    assert_equal_size_t(redis_parser_execute(p, &res, buf, len), len);
    assert(res != NULL);
    assert(res->type == REDIS_ERROR_T);
    assert(res->poff == 0);
    assert(res->plen == 8);
    assert(res->coff == 1);
    assert(res->clen == 5);
}

int main(int argc, char **argv) {
    redis_parser_t *parser = malloc(sizeof(redis_parser_t));
    redis_parser_init(parser, &callbacks);

    printf("redis_protocol_t: %lu bytes\n", sizeof(redis_protocol_t));
    printf("redis_parser_t: %lu bytes\n", sizeof(redis_parser_t));

    test_string(parser);
    test_empty_string(parser);
    test_array(parser);
    test_empty_array(parser);
    test_integer(parser);
    test_status(parser);
    test_error(parser);

    free(parser);
    return 0;
}
