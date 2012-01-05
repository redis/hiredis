#include "../fmacros.h"

/* misc */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

/* inet_pton */
#include <arpa/inet.h>

/* getaddrinfo */
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

/* local */
#include "../context.h"
#include "../object.h"
#include "test-helper.h"

#define SETUP_CONNECT()                                                        \
    redis_context c;                                                           \
    int rv;                                                                    \
    rv = redis_context_init(&c);                                               \
    assert(rv == REDIS_OK);                                                    \
    rv = redis_context_set_timeout(&c, 10000);                                 \
    assert(rv == REDIS_OK);

TEST(connect_in_refused) {
    SETUP_CONNECT();

    struct sockaddr_in sa;
    sa.sin_family = AF_INET;
    sa.sin_port = htons(redis_port() + 1);
    assert(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr) == 1);

    rv = redis_context_connect_in(&c, sa);
    assert_equal_int(rv, REDIS_ESYS);
    assert_equal_int(errno, ECONNREFUSED);

    /* Address shouldn't be set when no connection could be made */
    assert_equal_int(c.address.sa_family, 0);

    redis_context_destroy(&c);
}

TEST(connect_in6_refused) {
    SETUP_CONNECT();

    struct sockaddr_in6 sa;
    sa.sin6_family = AF_INET6;
    sa.sin6_port = htons(redis_port() + 1);
    assert(inet_pton(AF_INET6, "::1", &sa.sin6_addr) == 1);

    rv = redis_context_connect_in6(&c, sa);
    assert_equal_int(rv, REDIS_ESYS);
    assert_equal_int(errno, ECONNREFUSED);

    /* Address shouldn't be set when no connection could be made */
    assert_equal_int(c.address.sa_family, 0);

    redis_context_destroy(&c);
}

TEST(connect_un_noent) {
    SETUP_CONNECT();

    struct sockaddr_un sa;
    sa.sun_family = AF_LOCAL;
    strcpy((char*)&sa.sun_path, "/tmp/idontexist.sock");

    rv = redis_context_connect_un(&c, sa);
    assert_equal_int(rv, REDIS_ESYS);
    assert_equal_int(errno, ENOENT);

    /* Address shouldn't be set when no connection could be made */
    assert_equal_int(c.address.sa_family, 0);

    redis_context_destroy(&c);
}

TEST(connect_timeout) {
    SETUP_CONNECT();

    struct sockaddr_in sa;
    sa.sin_family = AF_INET;
    sa.sin_port = htons(redis_port());
    assert(inet_pton(AF_INET, "10.255.255.254", &sa.sin_addr) == 1);

    long long t1, t2;
    t1 = usec();
    rv = redis_context_connect_in(&c, sa);
    t2 = usec();

    assert_equal_int(rv, REDIS_ESYS);
    assert_equal_int(errno, ETIMEDOUT);
    assert((t2 - t1) < 15000); /* 5ms of slack should be enough */

    /* Address shouldn't be set when no connection could be made */
    assert_equal_int(c.address.sa_family, 0);

    redis_context_destroy(&c);
}

TEST(connect_success) {
    SETUP_CONNECT();

    struct sockaddr_in sa;
    sa.sin_family = AF_INET;
    sa.sin_port = htons(redis_port());
    assert(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr) == 1);

    rv = redis_context_connect_in(&c, sa);
    assert_equal_int(rv, REDIS_OK);

    /* Address should be set when connection was made */
    assert_equal_int(c.address.sa_family, AF_INET);

    redis_context_destroy(&c);
}

TEST(connect_gai_unknown_host) {
    SETUP_CONNECT();

    rv = redis_context_connect_gai(&c, "idontexist.foo", redis_port());
    assert_equal_int(rv, REDIS_EGAI);

    /* Don't care about the specific error for now. */
    assert(1);

    /* Address shouldn't be set when no connection could be made */
    assert_equal_int(c.address.sa_family, 0);

    redis_context_destroy(&c);
}

TEST(connect_gai_timeout) {
    SETUP_CONNECT();

    long long t1, t2;
    t1 = usec();
    rv = redis_context_connect_gai(&c, "10.255.255.254", redis_port());
    t2 = usec();

    assert_equal_int(rv, REDIS_ESYS);
    assert_equal_int(errno, ETIMEDOUT);
    assert((t2 - t1) < 15000); /* 5ms of slack should be enough */

    /* Address shouldn't be set when no connection could be made */
    assert_equal_int(c.address.sa_family, 0);

    redis_context_destroy(&c);
}

TEST(connect_gai_success) {
    SETUP_CONNECT();

    rv = redis_context_connect_gai(&c, "localhost", redis_port());
    assert_equal_int(rv, REDIS_OK);

    /* Address should be set when connection was made */
    assert_equal_int(c.address.sa_family, AF_INET);

    redis_context_destroy(&c);
}

#define SETUP_CONNECTED()                                                      \
    SETUP_CONNECT();                                                           \
    rv = redis_context_connect_gai(&c, "localhost", redis_port());             \
    assert_equal_int(rv, REDIS_OK);

static redis_object *read(redis_context *ctx) {
    redis_protocol *reply;
    int rv;

    rv = redis_context_read(ctx, &reply);
    assert_equal_return(rv, REDIS_OK);

    return (redis_object*)reply->data;
}

static void compare_ok_status(redis_object *obj) {
    assert_equal_int(obj->type, REDIS_STATUS);
    assert_equal_string(obj->str, "OK");
    assert_equal_int(obj->len, 2);
}

static void compare_string(redis_object *obj, const char *str, size_t len) {
    assert_equal_int(obj->type, REDIS_STRING);
    assert_equal_string(obj->str, str);
    assert_equal_int(obj->len, len);
}

TEST(write_command) {
    SETUP_CONNECTED();

    rv = redis_context_write_command(&c, "set foo bar");
    assert_equal_int(rv, REDIS_OK);
    compare_ok_status(read(&c));

    /* Check that the command was executed as intended */
    rv = redis_context_write_command(&c, "get foo");
    assert_equal_int(rv, REDIS_OK);
    compare_string(read(&c), "bar", 3);

    redis_context_destroy(&c);
}

TEST(write_command_argv) {
    SETUP_CONNECTED();

    int argc1 = 3;
    const char *argv1[] = { "set", "foo", "bar" };
    size_t argvlen1[] = { 3, 3, 3 };
    rv = redis_context_write_command_argv(&c, argc1, argv1, argvlen1);
    assert_equal_int(rv, REDIS_OK);
    compare_ok_status(read(&c));

    /* Check that the command was executed as intended */
    int argc2 = 2;
    const char *argv2[] = { "get", argv1[1] };
    size_t argvlen2[] = { 3, argvlen1[1] };
    rv = redis_context_write_command_argv(&c, argc2, argv2, argvlen2);
    assert_equal_int(rv, REDIS_OK);
    compare_string(read(&c), argv1[2], argvlen1[2]);

    redis_context_destroy(&c);
}

TEST(call_command) {
    SETUP_CONNECTED();

    redis_protocol *reply;

    rv = redis_context_call_command(&c, &reply, "set foo bar");
    assert_equal_int(rv, REDIS_OK);
    compare_ok_status(reply->data);

    /* Check that the command was executed as intended */
    rv = redis_context_call_command(&c, &reply, "get foo");
    assert_equal_int(rv, REDIS_OK);
    compare_string(reply->data, "bar", 3);

    redis_context_destroy(&c);
}

TEST(call_command_argv) {
    SETUP_CONNECTED();

    redis_protocol *reply;

    int argc1 = 3;
    const char *argv1[] = { "set", "foo", "bar" };
    size_t argvlen1[] = { 3, 3, 3 };
    rv = redis_context_call_command_argv(&c, &reply, argc1, argv1, argvlen1);
    assert_equal_int(rv, REDIS_OK);
    compare_ok_status(reply->data);

    /* Check that the command was executed as intended */
    int argc2 = 2;
    const char *argv2[] = { "get", argv1[1] };
    size_t argvlen2[] = { 3, argvlen1[1] };
    rv = redis_context_call_command_argv(&c, &reply, argc2, argv2, argvlen2);
    assert_equal_int(rv, REDIS_OK);
    compare_string(reply->data, argv1[2], argvlen1[2]);

    redis_context_destroy(&c);
}

int main(void) {
    test_connect_in_refused();
    test_connect_in6_refused();
    test_connect_un_noent();
    test_connect_timeout();
    test_connect_success();
    test_connect_gai_unknown_host();
    test_connect_gai_timeout();
    test_connect_gai_success();

    test_write_command();
    test_write_command_argv();
    test_call_command();
    test_call_command_argv();
}
