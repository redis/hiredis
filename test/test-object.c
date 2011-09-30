#include "../fmacros.h"

/* misc */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* local */
#include "test-helper.h"
#include "../parser.h"
#include "../object.h"

void test_string(redis_parser *parser) {
    const char *buf = "$5\r\nhello\r\n";
    redis_protocol *res;
    redis_object *obj;

    redis_parser_init(parser, &redis_object_parser_callbacks);
    assert(redis_parser_execute(parser, &res, buf, 11) == 11);

    obj = (redis_object*)res->data;
    assert(obj != NULL);

    assert_equal_int(obj->type, REDIS_STRING);
    assert_equal_string(obj->str, "hello");
    assert_equal_int(obj->len, 5);

    redis_object_free(&obj);
}

void test_chunked_string(redis_parser *parser) {
    const char *buf = "$5\r\nhello\r\n";
    redis_protocol *res;
    redis_object *obj;

    redis_parser_init(parser, &redis_object_parser_callbacks);
    assert(redis_parser_execute(parser, &res, buf+0, 6) == 6);
    assert(redis_parser_execute(parser, &res, buf+6, 5) == 5);

    obj = (redis_object*)res->data;
    assert(obj != NULL);

    assert_equal_int(obj->type, REDIS_STRING);
    assert_equal_string(obj->str, "hello");
    assert_equal_int(obj->len, 5);

    redis_object_free(&obj);
}

void test_empty_string(redis_parser *parser) {
    const char *buf = "$0\r\n\r\n";
    redis_protocol *res;
    redis_object *obj;

    redis_parser_init(parser, &redis_object_parser_callbacks);
    assert(redis_parser_execute(parser, &res, buf, 6) == 6);

    obj = (redis_object*)res->data;
    assert(obj != NULL);

    assert_equal_int(obj->type, REDIS_STRING);
    assert_equal_string(obj->str, "");
    assert_equal_int(obj->len, 0);

    redis_object_free(&obj);
}

void test_nil(redis_parser *parser) {
    const char *buf = "$-1\r\n";
    redis_protocol *res;
    redis_object *obj;

    redis_parser_init(parser, &redis_object_parser_callbacks);
    assert(redis_parser_execute(parser, &res, buf, 5) == 5);

    obj = (redis_object*)res->data;
    assert(obj != NULL);

    assert_equal_int(obj->type, REDIS_NIL);

    redis_object_free(&obj);
}

void test_array(redis_parser *parser) {
    const char *buf =
        "*2\r\n"
        "$5\r\nhello\r\n"
        "$5\r\nworld\r\n";
    redis_protocol *res;
    redis_object *obj;

    redis_parser_init(parser, &redis_object_parser_callbacks);
    assert(redis_parser_execute(parser, &res, buf, 26) == 26);

    obj = (redis_object*)res->data;
    assert(obj != NULL);

    assert_equal_int(obj->type, REDIS_ARRAY);
    assert_equal_int(obj->elements, 2);

    assert_equal_int(obj->element[0]->type, REDIS_STRING);
    assert_equal_string(obj->element[0]->str, "hello");
    assert_equal_int(obj->element[0]->len, 5);

    assert_equal_int(obj->element[1]->type, REDIS_STRING);
    assert_equal_string(obj->element[1]->str, "world");
    assert_equal_int(obj->element[1]->len, 5);

    redis_object_free(&obj);
}

void test_empty_array(redis_parser *parser) {
    const char *buf = "*0\r\n";
    redis_protocol *res;
    redis_object *obj;

    redis_parser_init(parser, &redis_object_parser_callbacks);
    assert(redis_parser_execute(parser, &res, buf, 4) == 4);

    obj = (redis_object*)res->data;
    assert(obj != NULL);

    assert_equal_int(obj->type, REDIS_ARRAY);
    assert_equal_int(obj->elements, 0);

    redis_object_free(&obj);
}

void test_integer(redis_parser *parser) {
    const char *buf = ":37\r\n";
    redis_protocol *res;
    redis_object *obj;

    redis_parser_init(parser, &redis_object_parser_callbacks);
    assert(redis_parser_execute(parser, &res, buf, 5) == 5);

    obj = (redis_object*)res->data;
    assert(obj != NULL);

    assert_equal_int(obj->type, REDIS_INTEGER);
    assert_equal_int(obj->integer, 37);

    redis_object_free(&obj);
}

void test_status(redis_parser *parser) {
    const char *buf = "+foo\r\n";
    redis_protocol *res;
    redis_object *obj;

    redis_parser_init(parser, &redis_object_parser_callbacks);
    assert(redis_parser_execute(parser, &res, buf, strlen(buf)) == strlen(buf));

    obj = (redis_object*)res->data;
    assert(obj != NULL);

    assert_equal_int(obj->type, REDIS_STATUS);
    assert_equal_string(obj->str, "foo");
    assert_equal_int(obj->len, 3);

    redis_object_free(&obj);
}

void test_empty_status(redis_parser *parser) {
    const char *buf = "+\r\n";
    redis_protocol *res;
    redis_object *obj;

    redis_parser_init(parser, &redis_object_parser_callbacks);
    assert(redis_parser_execute(parser, &res, buf, strlen(buf)) == strlen(buf));

    obj = (redis_object*)res->data;
    assert(obj != NULL);

    assert_equal_int(obj->type, REDIS_STATUS);
    assert_equal_string(obj->str, "");
    assert_equal_int(obj->len, 0);

    redis_object_free(&obj);
}

void test_error(redis_parser *parser) {
    const char *buf = "-err\r\n";
    redis_protocol *res;
    redis_object *obj;

    redis_parser_init(parser, &redis_object_parser_callbacks);
    assert(redis_parser_execute(parser, &res, buf, strlen(buf)) == strlen(buf));

    obj = (redis_object*)res->data;
    assert(obj != NULL);

    assert_equal_int(obj->type, REDIS_ERROR);
    assert_equal_string(obj->str, "err");
    assert_equal_int(obj->len, 3);

    redis_object_free(&obj);
}

void test_empty_error(redis_parser *parser) {
    const char *buf = "-\r\n";
    redis_protocol *res;
    redis_object *obj;

    redis_parser_init(parser, &redis_object_parser_callbacks);
    assert(redis_parser_execute(parser, &res, buf, strlen(buf)) == strlen(buf));

    obj = (redis_object*)res->data;
    assert(obj != NULL);

    assert_equal_int(obj->type, REDIS_ERROR);
    assert_equal_string(obj->str, "");
    assert_equal_int(obj->len, 0);

    redis_object_free(&obj);
}

void test_destroy_callback(redis_parser *parser) {
    const char *buf = "+ok\r";
    redis_protocol *res;

    redis_parser_init(parser, &redis_object_parser_callbacks);
    assert(redis_parser_execute(parser, &res, buf, strlen(buf)) == strlen(buf));
    assert(res == NULL);
    assert(redis_parser_err(parser) == RPE_OK);

    /* Go ahead and let the parser clean up */
    redis_parser_destroy(parser);

    /* No way to check if the temporary object gets properly free'd.
     * Let's hope that Valgrind starts complaining when this is buggy. */
}

int main(int argc, char **argv) {
    redis_parser *parser = malloc(sizeof(redis_parser));

    printf("redis_object: %lu bytes\n", sizeof(redis_object));

    test_string(parser);
    test_chunked_string(parser);
    test_empty_string(parser);

    test_nil(parser);

    test_array(parser);
    test_empty_array(parser);

    test_integer(parser);

    test_status(parser);
    test_empty_status(parser);

    test_error(parser);
    test_empty_error(parser);

    test_destroy_callback(parser);

    free(parser);
    return 0;
}
