#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "handle.h"
#include "test-helper.h"

TEST(connect_in_refused) {
    redis_handle h;
    int rv;

    rv = redis_handle_init(&h);
    assert(rv == REDIS_OK);

    struct sockaddr_in sa;
    sa.sin_family = AF_INET;
    sa.sin_port = htons(redis_port() + 1);
    assert(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr) == 1);

    rv = redis_handle_connect_in(&h, sa);
    assert_equal_int(rv, REDIS_OK);

    rv = redis_handle_wait_connected(&h);
    assert_equal_int(rv, REDIS_ESYS);
    assert_equal_int(errno, ECONNREFUSED);

    redis_handle_destroy(&h);
}

TEST(connect_in6_refused) {
    redis_handle h;
    int rv;

    rv = redis_handle_init(&h);
    assert(rv == REDIS_OK);

    struct sockaddr_in6 sa;
    sa.sin6_family = AF_INET6;
    sa.sin6_port = htons(redis_port() + 1);
    assert(inet_pton(AF_INET6, "::1", &sa.sin6_addr) == 1);

    rv = redis_handle_connect_in6(&h, sa);
    assert_equal_int(rv, REDIS_OK);

    rv = redis_handle_wait_connected(&h);
    assert_equal_int(rv, REDIS_ESYS);
    assert_equal_int(errno, ECONNREFUSED);

    redis_handle_destroy(&h);
}

TEST(connect_un_noent) {
    redis_handle h;
    int rv;

    rv = redis_handle_init(&h);
    assert(rv == REDIS_OK);

    struct sockaddr_un sa;
    sa.sun_family = AF_LOCAL;
    strcpy((char*)&sa.sun_path, "/tmp/idontexist.sock");

    rv = redis_handle_connect_un(&h, sa);
    assert_equal_int(rv, REDIS_ESYS);
    assert_equal_int(errno, ENOENT);

    redis_handle_destroy(&h);
}

TEST(connect_timeout) {
    redis_handle h;
    long long t1, t2;
    int rv;

    rv = redis_handle_init(&h);
    assert(rv == REDIS_OK);

    struct sockaddr_in sa;
    sa.sin_family = AF_INET;
    sa.sin_port = htons(redis_port());
    assert(inet_pton(AF_INET, "10.255.255.254", &sa.sin_addr) == 1);

    rv = redis_handle_set_timeout(&h, 10000);
    assert(rv == REDIS_OK);

    rv = redis_handle_connect_in(&h, sa);
    assert_equal_int(rv, REDIS_OK);

    t1 = usec();
    rv = redis_handle_wait_connected(&h);
    t2 = usec();

    assert_equal_int(rv, REDIS_ESYS);
    assert_equal_int(errno, ETIMEDOUT);
    assert((t2 - t1) < 15000); /* 5ms of slack should be enough */

    redis_handle_destroy(&h);
}

TEST(connect_gai_unknown_host) {
    redis_handle h;
    int rv;

    rv = redis_handle_init(&h);
    assert(rv == REDIS_OK);

    rv = redis_handle_connect_gai(&h, AF_INET, "idontexist.foo", redis_port(), NULL);
    assert_equal_int(rv, REDIS_EGAI);
    /* Don't care about the specific error for now. */

    redis_handle_destroy(&h);
}

TEST(connect_gai_success) {
    redis_handle h;
    int rv;

    rv = redis_handle_init(&h);
    assert(rv == REDIS_OK);

    rv = redis_handle_connect_gai(&h, AF_INET, "localhost", redis_port(), NULL);
    assert_equal_int(rv, REDIS_OK);

    rv = redis_handle_wait_connected(&h);
    assert_equal_int(rv, REDIS_OK);

    redis_handle_destroy(&h);
}

TEST(connect_gai_redis_address) {
    redis_handle h;
    redis_address address;
    int rv;

    rv = redis_handle_init(&h);
    assert(rv == REDIS_OK);

    rv = redis_handle_connect_gai(&h, AF_INET, "localhost", redis_port(), &address);
    assert_equal_int(rv, REDIS_OK);

    /* Match common fields */
    assert(address.sa_family == AF_INET);
    assert(address.sa_addrlen == sizeof(struct sockaddr_in));

    /* Match sockaddr specific fields */
    assert(address.sa_addr.in.sin_family == AF_INET);
    assert(ntohs(address.sa_addr.in.sin_port) == redis_port());
    assert(strcmp(inet_ntoa(address.sa_addr.in.sin_addr), "127.0.0.1") == 0);

    redis_handle_destroy(&h);
}

redis_handle *setup(void) {
    redis_handle *h = malloc(sizeof(*h));
    int rv;

    rv = redis_handle_init(h);
    assert(rv == REDIS_OK);
    rv = redis_handle_set_timeout(h, 10000);
    assert_equal_int(rv, REDIS_OK);
    rv = redis_handle_connect_gai(h, AF_INET, "localhost", redis_port(), NULL);
    assert_equal_int(rv, REDIS_OK);
    rv = redis_handle_wait_connected(h);
    assert_equal_int(rv, REDIS_OK);

    return h;
}

void teardown(redis_handle *h) {
    redis_handle_destroy(h);
    free(h);
}

TEST(eof_after_quit) {
    redis_handle *h = setup();
    int rv, drained;

    rv = redis_handle_write_to_buffer(h, "quit\r\n", 6);
    assert(rv == REDIS_OK);
    rv = redis_handle_write_from_buffer(h, &drained);
    assert(rv == REDIS_OK && drained);
    rv = redis_handle_wait_readable(h);
    assert(rv == REDIS_OK);
    rv = redis_handle_read_to_buffer(h);
    assert(rv == REDIS_OK);

    redis_protocol *p;
    rv = redis_handle_read_from_buffer(h, &p);
    assert(rv == REDIS_OK);
    assert_equal_int(p->type, REDIS_STATUS);
    assert_equal_int(p->plen, 5); /* +ok\r\n */

    /* wait_readable should return REDIS_OK because EOF is readable */
    rv = redis_handle_wait_readable(h);
    assert_equal_int(rv, REDIS_OK);

    /* 1: read EOF */
    rv = redis_handle_read_to_buffer(h);
    assert_equal_int(rv, REDIS_EEOF);

    /* 2: reading EOF should be idempotent (user is responsible for closing the handle) */
    rv = redis_handle_read_to_buffer(h);
    assert_equal_int(rv, REDIS_EEOF);

    teardown(h);
}

TEST(read_timeout) {
    redis_handle *h = setup();
    int rv;

    rv = redis_handle_wait_readable(h);
    assert_equal_int(rv, REDIS_ESYS);
    assert_equal_int(errno, ETIMEDOUT);

    teardown(h);
}

TEST(einval_against_closed_handle) {
    redis_handle *h = setup();
    int rv;

    rv = redis_handle_close(h);
    assert_equal_int(rv, REDIS_OK);

    rv = redis_handle_write_to_buffer(h, "ping\r\n", 6);
    assert_equal_int(rv, REDIS_ESYS);
    assert_equal_int(errno, EINVAL);

    int drained;
    rv = redis_handle_write_from_buffer(h, &drained);
    assert_equal_int(rv, REDIS_ESYS);
    assert_equal_int(errno, EINVAL);

    rv = redis_handle_read_to_buffer(h);
    assert_equal_int(rv, REDIS_ESYS);
    assert_equal_int(errno, EINVAL);

    redis_protocol *p;
    rv = redis_handle_read_from_buffer(h, &p);
    assert_equal_int(rv, REDIS_ESYS);
    assert_equal_int(errno, EINVAL);

    teardown(h);
}

int main(void) {
    test_connect_in_refused();
    test_connect_in6_refused();
    test_connect_un_noent();
    test_connect_timeout();
    test_connect_gai_unknown_host();
    test_connect_gai_success();
    test_connect_gai_redis_address();

    test_eof_after_quit();
    test_read_timeout();
    test_einval_against_closed_handle();
}
