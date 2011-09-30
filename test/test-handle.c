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
#include "../handle.h"
#include "test-helper.h"

int test_connect_in_refused(void) {
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
    return 0;
}

int test_connect_in6_refused(void) {
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
    return 0;
}

int test_connect_un_noent(void) {
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
    return 0;
}

int test_connect_timeout(void) {
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
    return 0;
}

int test_connect_gai_unknown_host(void) {
    redis_handle h;
    int rv;

    rv = redis_handle_init(&h);
    assert(rv == REDIS_OK);

    rv = redis_handle_connect_gai(&h, AF_INET, "idontexist.foo", redis_port());
    assert_equal_int(rv, REDIS_EGAI);
    /* Don't care about the specific error for now. */

    redis_handle_destroy(&h);
    return 0;
}

int test_connect_gai_success(void) {
    redis_handle h;
    int rv;

    rv = redis_handle_init(&h);
    assert(rv == REDIS_OK);

    rv = redis_handle_connect_gai(&h, AF_INET, "localhost", redis_port());
    assert_equal_int(rv, REDIS_OK);

    rv = redis_handle_wait_connected(&h);
    assert_equal_int(rv, REDIS_OK);

    redis_handle_destroy(&h);
    return 0;
}

int main(void) {
    test_connect_in_refused();
    test_connect_in6_refused();
    test_connect_un_noent();
    test_connect_timeout();
    test_connect_gai_unknown_host();
    test_connect_gai_success();
}
