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

/* close */
#include <unistd.h>

/* local */
#include "../context.h"
#include "../object.h"
#include "test-helper.h"
#include "net-helper.h"

#define SETUP_CONNECT()                                                        \
    redis_context c;                                                           \
    int rv;                                                                    \
    rv = redis_context_init(&c);                                               \
    assert_equal_return(rv, REDIS_OK);                                         \
    rv = redis_context_set_timeout(&c, 10000);                                 \
    assert_equal_return(rv, REDIS_OK);

TEST(connect_in_refused) {
    SETUP_CONNECT();

    redis_address addr = redis_address_in("127.0.0.1", redis_port() + 1);
    rv = redis_context_connect_in(&c, addr.sa_addr.in);
    assert_equal_return(rv, REDIS_ESYS);
    assert_equal_int(errno, ECONNREFUSED);

    /* Address shouldn't be set when no connection could be made */
    assert_equal_int(c.address.sa_family, 0);

    redis_context_destroy(&c);
}

TEST(connect_in6_refused) {
    SETUP_CONNECT();

    redis_address addr = redis_address_in6("::1", redis_port() + 1);
    rv = redis_context_connect_in6(&c, addr.sa_addr.in6);
    assert_equal_return(rv, REDIS_ESYS);
    assert_equal_int(errno, ECONNREFUSED);

    /* Address shouldn't be set when no connection could be made */
    assert_equal_int(c.address.sa_family, 0);

    redis_context_destroy(&c);
}

TEST(connect_un_noent) {
    SETUP_CONNECT();

    redis_address addr = redis_address_un("/tmp/idontexist.sock");
    rv = redis_context_connect_un(&c, addr.sa_addr.un);
    assert_equal_return(rv, REDIS_ESYS);
    assert_equal_int(errno, ENOENT);

    /* Address shouldn't be set when no connection could be made */
    assert_equal_int(c.address.sa_family, 0);

    redis_context_destroy(&c);
}

TEST(connect_timeout) {
    SETUP_CONNECT();

    redis_address addr = redis_address_in("10.255.255.254", redis_port());

    long long t1, t2;
    t1 = usec();
    rv = redis_context_connect_in(&c, addr.sa_addr.in);
    t2 = usec();

    assert_equal_return(rv, REDIS_ESYS);
    assert_equal_int(errno, ETIMEDOUT);
    assert((t2 - t1) < 15000); /* 5ms of slack should be enough */

    /* Address shouldn't be set when no connection could be made */
    assert_equal_int(c.address.sa_family, 0);

    redis_context_destroy(&c);
}

TEST(connect_success) {
    SETUP_CONNECT();

    redis_address addr = redis_address_in("127.0.0.1", redis_port());
    rv = redis_context_connect_in(&c, addr.sa_addr.in);
    assert_equal_return(rv, REDIS_OK);

    /* Address should be set when connection was made */
    assert_equal_int(c.address.sa_family, AF_INET);

    redis_context_destroy(&c);
}

TEST(connect_gai_unknown_host) {
    SETUP_CONNECT();

    rv = redis_context_connect_gai(&c, "idontexist.foo", redis_port());
    assert_equal_return(rv, REDIS_EGAI);

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

    assert_equal_return(rv, REDIS_ESYS);
    assert_equal_int(errno, ETIMEDOUT);
    assert((t2 - t1) < 15000); /* 5ms of slack should be enough */

    /* Address shouldn't be set when no connection could be made */
    assert_equal_int(c.address.sa_family, 0);

    redis_context_destroy(&c);
}

TEST(connect_gai_success) {
    SETUP_CONNECT();

    rv = redis_context_connect_gai(&c, "localhost", redis_port());
    assert_equal_return(rv, REDIS_OK);

    /* Address should be set when connection was made */
    assert_equal_int(c.address.sa_family, AF_INET);

    redis_context_destroy(&c);
}

#define SETUP_CONNECTED()                                                      \
    SETUP_CONNECT();                                                           \
    rv = redis_context_connect_gai(&c, "localhost", redis_port());             \
    assert_equal_return(rv, REDIS_OK);

static redis_object *read_from_context(redis_context *ctx) {
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
    assert_equal_return(rv, REDIS_OK);
    compare_ok_status(read_from_context(&c));

    /* Check that the command was executed as intended */
    rv = redis_context_write_command(&c, "get foo");
    assert_equal_return(rv, REDIS_OK);
    compare_string(read_from_context(&c), "bar", 3);

    redis_context_destroy(&c);
}

TEST(write_command_argv) {
    SETUP_CONNECTED();

    int argc1 = 3;
    const char *argv1[] = { "set", "foo", "bar" };
    size_t argvlen1[] = { 3, 3, 3 };
    rv = redis_context_write_command_argv(&c, argc1, argv1, argvlen1);
    assert_equal_return(rv, REDIS_OK);
    compare_ok_status(read_from_context(&c));

    /* Check that the command was executed as intended */
    int argc2 = 2;
    const char *argv2[] = { "get", argv1[1] };
    size_t argvlen2[] = { 3, argvlen1[1] };
    rv = redis_context_write_command_argv(&c, argc2, argv2, argvlen2);
    assert_equal_return(rv, REDIS_OK);
    compare_string(read_from_context(&c), argv1[2], argvlen1[2]);

    redis_context_destroy(&c);
}

TEST(call_command) {
    SETUP_CONNECTED();

    redis_protocol *reply;

    rv = redis_context_call_command(&c, &reply, "set foo bar");
    assert_equal_return(rv, REDIS_OK);
    compare_ok_status(reply->data);

    /* Check that the command was executed as intended */
    rv = redis_context_call_command(&c, &reply, "get foo");
    assert_equal_return(rv, REDIS_OK);
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
    assert_equal_return(rv, REDIS_OK);
    compare_ok_status(reply->data);

    /* Check that the command was executed as intended */
    int argc2 = 2;
    const char *argv2[] = { "get", argv1[1] };
    size_t argvlen2[] = { 3, argvlen1[1] };
    rv = redis_context_call_command_argv(&c, &reply, argc2, argv2, argvlen2);
    assert_equal_return(rv, REDIS_OK);
    compare_string(reply->data, argv1[2], argvlen1[2]);

    redis_context_destroy(&c);
}

TEST(flush_against_full_kernel_buffer) {
    SETUP_CONNECT();

    redis_address addr = redis_address_in("127.0.0.1", redis_port() + 1);

    run_server_args args;
    args.address = addr;

    spawn(run_server, &args);

    rv = redis_context_connect_in(&c, addr.sa_addr.in);
    assert_equal_return(rv, REDIS_OK);

    /* Now write and flush until error */
    while (1) {
        rv = redis_context_write_command(&c, "ping");
        assert_equal_return(rv, REDIS_OK);

        rv = redis_context_flush(&c);
        if (rv != REDIS_OK) {
            break;
        }
    }

    /* When the write buffer cannot be flushed, the operation should time out
     * instead of directly returning EAGAIN to the caller */
    assert_equal_return(rv, REDIS_ESYS);
    assert_equal_int(errno, ETIMEDOUT);
}

void close_after_accept(int fd, void *data) {
    close(fd);
}

TEST(read_against_closed_connection) {
    SETUP_CONNECT();

    redis_address addr = redis_address_in("127.0.0.1", redis_port() + 1);

    run_server_args args;
    args.address = addr;
    args.fn.ptr = close_after_accept;

    spawn(run_server, &args);

    rv = redis_context_connect_in(&c, addr.sa_addr.in);
    assert_equal_return(rv, REDIS_OK);

    redis_protocol *reply = NULL;

    /* Read should immediately EOF */
    rv = redis_context_read(&c, &reply);
    assert_equal_return(rv, REDIS_EEOF);

    /* ... and should be idempotent... */
    rv = redis_context_read(&c, &reply);
    assert_equal_return(rv, REDIS_EEOF);

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
    test_flush_against_full_kernel_buffer();
    test_read_against_closed_connection();
}
