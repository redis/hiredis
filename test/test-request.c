#include "../fmacros.h"

/* misc */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

/* local */
#include "../request.h"
#include "test-helper.h"

static struct request_to_write_cb_s {
    int callc;
    struct {
        redis_request_queue *q;
        redis_request *r;
    } *callv;
} request_to_write_cb_calls = { .callc = 0, .callv = NULL };

void request_to_write_cb_reset(void) {
    struct request_to_write_cb_s *s = &request_to_write_cb_calls;

    if (s->callc) {
        free(s->callv);
    }

    s->callc = 0;
    s->callv = NULL;
}

void request_to_write_cb(redis_request_queue *q, redis_request *r) {
    struct request_to_write_cb_s *s = &request_to_write_cb_calls;

    s->callv = realloc(s->callv, sizeof(*s->callv) * (s->callc + 1));
    assert(s->callv != NULL);

    s->callv[s->callc].q = q;
    s->callv[s->callc].r = r;
    s->callc++;
}

static struct request_wait_write_cb_s {
    int callc;
    struct {
        redis_request_queue *q;
        redis_request *r;
    } *callv;
} request_wait_write_cb_calls = { .callc = 0, .callv = NULL };

void request_wait_write_cb_reset(void) {
    struct request_wait_write_cb_s *s = &request_wait_write_cb_calls;

    if (s->callc) {
        free(s->callv);
    }

    s->callc = 0;
    s->callv = NULL;
}

void request_wait_write_cb(redis_request_queue *q, redis_request *r) {
    struct request_wait_write_cb_s *s = &request_wait_write_cb_calls;

    s->callv = realloc(s->callv, sizeof(*s->callv) * (s->callc + 1));
    assert(s->callv != NULL);

    s->callv[s->callc].q = q;
    s->callv[s->callc].r = r;
    s->callc++;
}

static struct request_wait_read_cb_s {
    int callc;
    struct {
        redis_request_queue *q;
        redis_request *r;
    } *callv;
} request_wait_read_cb_calls = { .callc = 0, .callv = NULL };

void request_wait_read_cb_reset(void) {
    struct request_wait_read_cb_s *s = &request_wait_read_cb_calls;

    if (s->callc) {
        free(s->callv);
    }

    s->callc = 0;
    s->callv = NULL;
}

void request_wait_read_cb(redis_request_queue *q, redis_request *r) {
    struct request_wait_read_cb_s *s = &request_wait_read_cb_calls;

    s->callv = realloc(s->callv, sizeof(*s->callv) * (s->callc + 1));
    assert(s->callv != NULL);

    s->callv[s->callc].q = q;
    s->callv[s->callc].r = r;
    s->callc++;
}

static struct write_fn_s {
    int callc;
    struct {
        redis_request_queue *q;
    } *callv;
} write_fn_calls = { .callc = 0, .callv = NULL };

void write_fn_reset(void) {
    struct write_fn_s *s = &write_fn_calls;

    if (s->callc) {
        free(s->callv);
    }

    s->callc = 0;
    s->callv = NULL;
}

void write_fn(redis_request_queue *q) {
    struct write_fn_s *s = &write_fn_calls;

    s->callv = realloc(s->callv, sizeof(*s->callv) * (s->callc + 1));
    assert(s->callv != NULL);

    s->callv[s->callc].q = q;
    s->callc++;
}

#define SETUP()                                                               \
    redis_request_queue q;                                                    \
    int rv;                                                                   \
                                                                              \
    request_to_write_cb_reset();                                              \
    request_wait_write_cb_reset();                                            \
    request_wait_read_cb_reset();                                             \
    write_fn_reset();                                                         \
                                                                              \
    rv = redis_request_queue_init(&q);                                        \
    assert_equal_return(rv, REDIS_OK);                                        \
                                                                              \
    q.request_to_write_cb = request_to_write_cb;                              \
    q.request_wait_write_cb = request_wait_write_cb;                          \
    q.request_wait_read_cb = request_wait_read_cb;                            \
    q.write_fn = write_fn;

#define TEARDOWN()                                                            \
    redis_request_queue_destroy(&q);

typedef struct t1_redis_request_s t1_redis_request;

struct t1_redis_request_s {
    REDIS_REQUEST_COMMON

    const char *buf;
    size_t len;
    size_t emit;
    size_t nemitted;
    size_t nwritten;

    const char *read_raw_buf;
    size_t read_raw_len;

    redis_protocol *reply;

    int free_calls;
};

void t1_write_ptr(redis_request *_self, const char **buf, size_t *len, int *done) {
    t1_redis_request *self = (t1_redis_request*)_self;
    size_t to_emit;

    assert(self->nemitted < self->len);

    to_emit = self->len - self->nemitted;
    if (to_emit > self->emit) {
        to_emit = self->emit;
    }

    *buf = self->buf + self->nemitted;
    *len = to_emit;
    self->nemitted += to_emit;

    if (self->nemitted == self->len) {
        *done = 1;
    }
}

int t1_write_cb(redis_request *_self, int n, int *done) {
    t1_redis_request *self = (t1_redis_request*)_self;
    int to_write;

    to_write = self->len - self->nwritten;
    if (to_write > n) {
        to_write = n;
    }

    self->nwritten += to_write;

    if (self->nwritten == self->len) {
        *done = 1;
    }

    return to_write;
}

int t1_read_cb(redis_request *_self, const char *buf, size_t len, int *done) {
    t1_redis_request *self = (t1_redis_request*)_self;
    redis_protocol *p = NULL;
    size_t nparsed;

    nparsed = redis_parser_execute(&self->request_queue->parser, &p, buf, len);

    /* Test for parse error */
    if (nparsed < len && p == NULL) {
        errno = redis_parser_err(&self->request_queue->parser);
        return REDIS_EPARSER;
    }

    if (p != NULL) {
        self->reply = p;
        *done = 1;
    }

    self->read_raw_buf = buf;
    self->read_raw_len = nparsed;

    return nparsed;
}

void t1_free(redis_request *_self) {
    t1_redis_request *self = (t1_redis_request*)_self;
    self->free_calls++;
}

void t1_init(t1_redis_request *self) {
    memset(self, 0, sizeof(*self));
    redis_request_init((redis_request*)self);

    self->write_ptr = t1_write_ptr;
    self->write_cb = t1_write_cb;
    self->read_cb = t1_read_cb;
    self->free = t1_free;
}

TEST(insert_request) {
    SETUP();

    t1_redis_request req;
    t1_init(&req);

    req.buf = "hello";
    req.len = strlen(req.buf);
    req.emit = 2;

    redis_request_queue_insert(&q, (redis_request*)&req);

    /* Test that callback was correctly triggered */
    assert_equal_int(request_to_write_cb_calls.callc, 1);
    assert(request_to_write_cb_calls.callv[0].q == &q);
    assert(request_to_write_cb_calls.callv[0].r == (redis_request*)&req);

    TEARDOWN();
}

#define INIT_REQUEST(_var, _str)                                              \
    t1_redis_request (_var);                                                  \
    t1_init(&(_var));                                                         \
    (_var).buf = (_str);                                                      \
    (_var).len = strlen((_var).buf);                                          \
    (_var).emit = (_var).len;

#define SETUP_INSERTED()                                                      \
    SETUP();                                                                  \
    INIT_REQUEST(req1, "hello");                                              \
    INIT_REQUEST(req2, "world");                                              \
    redis_request_queue_insert(&q, (redis_request*)&req1);                    \
    redis_request_queue_insert(&q, (redis_request*)&req2);                    \

TEST(write_ptr) {
    SETUP_INSERTED();

    const char *buf;
    size_t len;

    req1.emit = 3;
    req2.emit = 3;

    rv = redis_request_queue_write_ptr(&q, &buf, &len);
    assert_equal_size_t(rv, 0);
    assert_equal_size_t(len, 3);
    assert(strncmp(buf, "hel", len) == 0);
    assert(req1.write_ptr_done == 0);

    /* The first request should have moved to the wait_write queue now */
    assert_equal_int(request_wait_write_cb_calls.callc, 1);
    assert(request_wait_write_cb_calls.callv[0].q == &q);
    assert(request_wait_write_cb_calls.callv[0].r == (redis_request*)&req1);

    rv = redis_request_queue_write_ptr(&q, &buf, &len);
    assert_equal_size_t(rv, 0);
    assert_equal_size_t(len, 2);
    assert(strncmp(buf, "lo", len) == 0);
    assert(req1.write_ptr_done == 1);

    rv = redis_request_queue_write_ptr(&q, &buf, &len);
    assert_equal_size_t(rv, 0);
    assert_equal_size_t(len, 3);
    assert(strncmp(buf, "wor", len) == 0);
    assert(req2.write_ptr_done == 0);

    /* The second request should have moved to the wait_write queue now */
    assert_equal_int(request_wait_write_cb_calls.callc, 2);
    assert(request_wait_write_cb_calls.callv[1].q == &q);
    assert(request_wait_write_cb_calls.callv[1].r == (redis_request*)&req2);

    rv = redis_request_queue_write_ptr(&q, &buf, &len);
    assert_equal_size_t(rv, 0);
    assert_equal_size_t(len, 2);
    assert(strncmp(buf, "ld", len) == 0);
    assert(req2.write_ptr_done == 1);

    rv = redis_request_queue_write_ptr(&q, &buf, &len);
    assert_equal_size_t(rv, -1);

    TEARDOWN();
}

#define SETUP_WRITTEN_UNCONFIRMED()                                           \
    SETUP_INSERTED();                                                         \
    do {                                                                      \
        const char *buf;                                                      \
        size_t len;                                                           \
        rv = redis_request_queue_write_ptr(&q, &buf, &len);                   \
    } while (rv == 0);

TEST(write_cb) {
    SETUP_WRITTEN_UNCONFIRMED();

    /* Assume 3 bytes were written ("hel" in req1) */
    rv = redis_request_queue_write_cb(&q, 3);
    assert_equal_size_t(rv, 0);
    assert_equal_size_t(req1.nwritten, 3);
    assert_equal_size_t(req2.nwritten, 0);
    assert(req1.write_cb_done == 0);
    assert(req2.write_cb_done == 0);

    /* The first request should have moved to the wait_read queue now */
    assert_equal_int(request_wait_read_cb_calls.callc, 1);
    assert(request_wait_read_cb_calls.callv[0].q == &q);
    assert(request_wait_read_cb_calls.callv[0].r == (redis_request*)&req1);

    /* Assume another 3 bytes were written ("lo" in req1, "w" in req2) */
    rv = redis_request_queue_write_cb(&q, 3);
    assert_equal_size_t(rv, 0);
    assert_equal_size_t(req1.nwritten, 5);
    assert_equal_size_t(req2.nwritten, 1);
    assert(req1.write_cb_done == 1);
    assert(req2.write_cb_done == 0);

    /* The second request should have moved to the wait_read queue now */
    assert_equal_int(request_wait_read_cb_calls.callc, 2);
    assert(request_wait_read_cb_calls.callv[1].q == &q);
    assert(request_wait_read_cb_calls.callv[1].r == (redis_request*)&req2);

    /* Run callback for remaining bytes */
    rv = redis_request_queue_write_cb(&q, 4);
    assert_equal_size_t(rv, 0);
    assert_equal_size_t(req1.nwritten, 5);
    assert_equal_size_t(req2.nwritten, 5);
    assert(req1.write_cb_done == 1);
    assert(req2.write_cb_done == 1);

    /* More bytes cannot be mapped to requests... */
    rv = redis_request_queue_write_cb(&q, 4);
    assert_equal_size_t(rv, -1);

    TEARDOWN();
}

#define SETUP_WRITTEN_CONFIRMED()                                             \
    SETUP_WRITTEN_UNCONFIRMED();                                              \
    rv = redis_request_queue_write_cb(&q, req1.len);                          \
    assert_equal_size_t(rv, 0);                                               \
    rv = redis_request_queue_write_cb(&q, req2.len);                          \
    assert_equal_size_t(rv, 0);

TEST(read_cb) {
    SETUP_WRITTEN_CONFIRMED();

    /* Feed part of the reply for request 1 */
    rv = redis_request_queue_read_cb(&q, "+stat", 5);
    assert_equal_size_t(rv, 0);

    assert_equal_size_t(req1.read_raw_len, 5);
    assert(strncmp(req1.read_raw_buf, "+stat", req1.read_raw_len) == 0);
    assert(req1.reply == NULL);
    assert(req1.free_calls == 0);
    assert(req1.read_cb_done == 0);

    /* Feed remaining part for request 1, and first part for request 2 */
    rv = redis_request_queue_read_cb(&q, "us\r\n+st", 7);
    assert_equal_size_t(rv, 0);

    assert_equal_size_t(req1.read_raw_len, 4);
    assert(strncmp(req1.read_raw_buf, "us\r\n", req1.read_raw_len) == 0);
    assert(req1.reply != NULL && req1.reply->type == REDIS_STATUS);
    assert(req1.free_calls == 1);
    assert(req1.read_cb_done == 1);

    assert_equal_size_t(req2.read_raw_len, 3);
    assert(strncmp(req2.read_raw_buf, "+st", req2.read_raw_len) == 0);
    assert(req2.reply == NULL);
    assert(req2.free_calls == 0);
    assert(req2.read_cb_done == 0);

    /* Feed remaining part for request 2 */
    rv = redis_request_queue_read_cb(&q, "atus\r\n", 6);
    assert_equal_size_t(rv, 0);

    assert_equal_size_t(req2.read_raw_len, 6);
    assert(strncmp(req2.read_raw_buf, "atus\r\n", req2.read_raw_len) == 0);
    assert(req2.reply != NULL && req1.reply->type == REDIS_STATUS);
    assert(req2.free_calls == 1);
    assert(req2.read_cb_done == 1);

    TEARDOWN();
}

TEST(read_cb_with_parse_error) {
    SETUP_WRITTEN_CONFIRMED();

    /* Feed part of an erroneous reply */
    rv = redis_request_queue_read_cb(&q, "+x\r\r", 4);
    assert_equal_return(rv, REDIS_EPARSER);

    /* This should keep failing */
    rv = redis_request_queue_read_cb(&q, "\n", 1);
    assert_equal_return(rv, REDIS_EPARSER);

    TEARDOWN();
}

TEST(read_cb_with_pending_write_ptr_emit) {
    SETUP_INSERTED();

    const char *buf;
    size_t len;

    req1.emit = 3;

    /* Grab first 3 bytes from request */
    rv = redis_request_queue_write_ptr(&q, &buf, &len);
    assert_equal_size_t(rv, 0);
    assert_equal_size_t(len, 3);
    assert(strncmp(buf, "hel", len) == 0);

    /* Assume 3 bytes were written ("hel" in req1) */
    rv = redis_request_queue_write_cb(&q, 3);
    assert_equal_size_t(rv, 0);
    assert_equal_size_t(req1.nwritten, 3);

    /* Feed part of the reply for request 1 */
    rv = redis_request_queue_read_cb(&q, "+stat", 5);
    assert_equal_size_t(rv, 0);

    /* Grab remaining bytes from request */
    rv = redis_request_queue_write_ptr(&q, &buf, &len);
    assert_equal_size_t(rv, 0);
    assert_equal_size_t(len, 2);
    assert(strncmp(buf, "lo", len) == 0);

    /* Assume remaining bytes were written */
    rv = redis_request_queue_write_cb(&q, 2);
    assert_equal_size_t(rv, 0);
    assert_equal_size_t(req1.nwritten, 5);

    /* Feed remaining part of the reply for request 1 */
    rv = redis_request_queue_read_cb(&q, "us\r\n", 4);
    assert_equal_size_t(rv, 0);

    assert(req1.reply != NULL && req1.reply->type == REDIS_STATUS);
    assert(req1.free_calls == 1);

    TEARDOWN();
}

TEST(write_fn_call_on_first_insert) {
    SETUP();

    INIT_REQUEST(req, "hello");

    redis_request_queue_insert(&q, (redis_request*)&req);

    /* Test that the write fn was called */
    assert_equal_int(write_fn_calls.callc, 1);
    assert(write_fn_calls.callv[0].q == &q);

    TEARDOWN();
}

TEST(write_fn_no_call_on_second_insert) {
    SETUP();

    INIT_REQUEST(req1, "hello");
    INIT_REQUEST(req2, "world");

    redis_request_queue_insert(&q, (redis_request*)&req1);
    redis_request_queue_insert(&q, (redis_request*)&req2);

    /* Test that the write fn wasn't called twice */
    assert_equal_int(write_fn_calls.callc, 1);
    assert(write_fn_calls.callv[0].q == &q);

    TEARDOWN();
}

TEST(write_fn_call_after_drain) {
    SETUP_WRITTEN_CONFIRMED();

    INIT_REQUEST(reqN, "hi");

    redis_request_queue_insert(&q, (redis_request*)&reqN);

    /* Test that the write fn was called */
    assert_equal_int(write_fn_calls.callc, 2);
    assert(write_fn_calls.callv[0].q == &q);

    TEARDOWN();
}

int main(void) {
    test_insert_request();
    test_write_ptr();
    test_write_cb();
    test_read_cb();
    test_read_cb_with_parse_error();
    test_read_cb_with_pending_write_ptr_emit();
    test_write_fn_call_on_first_insert();
    test_write_fn_no_call_on_second_insert();
    test_write_fn_call_after_drain();
}
