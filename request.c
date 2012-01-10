#include <string.h>
#include <assert.h>
#include <errno.h>

#include "handle.h" /* return values */
#include "request.h"

int redis_request_init(redis_request *self) {
    memset(self, 0, sizeof(*self));
    return REDIS_OK;
}

int redis_request_destroy(redis_request *self) {
    ((void) self);
    return REDIS_OK;
}

int redis_request_queue_init(redis_request_queue *self) {
    memset(self, 0, sizeof(*self));
    ngx_queue_init(&self->request_to_write);
    ngx_queue_init(&self->request_wait_write);
    ngx_queue_init(&self->request_wait_read);
    redis_parser_init(&self->parser, NULL);
    return REDIS_OK;
}

void redis__free_queue(ngx_queue_t *h) {
    ngx_queue_t *q;
    redis_request *req;

    ngx_queue_foreach(q, h) {
        ngx_queue_remove(q);
        req = ngx_queue_data(q, redis_request, queue);
        req->free(req);
    }
}

int redis_request_queue_destroy(redis_request_queue *self) {
    redis__free_queue(&self->request_to_write);
    redis__free_queue(&self->request_wait_write);
    redis__free_queue(&self->request_wait_read);
    redis_parser_destroy(&self->parser);
    return REDIS_OK;
}

void redis_request_queue_insert(redis_request_queue *self, redis_request *request) {
    ngx_queue_insert_head(&self->request_to_write, &request->queue);

    if (self->request_to_write_cb) {
        self->request_to_write_cb(self, request);
    }
}

redis_request *redis__request_queue_move(ngx_queue_t *a, ngx_queue_t *b) {
    ngx_queue_t *q;

    /* Unable to move requests when there are none... */
    if (ngx_queue_empty(a)) {
        return NULL;
    }

    q = ngx_queue_last(a);
    ngx_queue_remove(q);
    ngx_queue_insert_head(b, q);
    return ngx_queue_data(q, redis_request, queue);
}

redis_request *redis__request_queue_pop_to_write(redis_request_queue *self) {
    redis_request *req;

    req = redis__request_queue_move(&self->request_to_write,
                                    &self->request_wait_write);

    if (req && self->request_wait_write_cb) {
        self->request_wait_write_cb(self, req);
    }

    return req;
}

int redis_request_queue_write_ptr(redis_request_queue *self, const char **buf, size_t *len) {
    ngx_queue_t *q;
    redis_request *req;

    /* We need at least one element in the wait_write queue */
    if (ngx_queue_empty(&self->request_wait_write)) {
        if (redis__request_queue_pop_to_write(self) == NULL) {
            return -1;
        }
    }

    while (1) {
        assert(!ngx_queue_empty(&self->request_wait_write));
        q = ngx_queue_head(&self->request_wait_write);
        req = ngx_queue_data(q, redis_request, queue);

        const char *auxbuf = NULL;
        size_t auxlen = 0;

        assert(req->write_ptr);
        req->write_ptr(req, &auxbuf, &auxlen);
        if (auxbuf == NULL) {
            if (redis__request_queue_pop_to_write(self) == NULL) {
                return -1;
            }

            continue;
        }

        *buf = auxbuf;
        *len = auxlen;
        break;
    }

    return 0;
}

redis_request *redis__request_queue_pop_wait_write(redis_request_queue *self) {
    redis_request *req;

    req = redis__request_queue_move(&self->request_wait_write,
                                    &self->request_wait_read);

    if (req && self->request_wait_read_cb) {
        self->request_wait_read_cb(self, req);
    }

    return req;
}

int redis_request_queue_write_cb(redis_request_queue *self, int n) {
    ngx_queue_t *q;
    redis_request *req;
    int wrote;

    /* We need at least one element in the wait_read queue */
    if (ngx_queue_empty(&self->request_wait_read)) {
        if (redis__request_queue_pop_wait_write(self) == NULL) {
            /* The request cannot be NULL: it emitted bytes to write and should
             * be present in the wait_write queue, waiting for a callback. */
            return -1;
        }
    }

    while (n) {
        assert(!ngx_queue_empty(&self->request_wait_read));
        q = ngx_queue_head(&self->request_wait_read);
        req = ngx_queue_data(q, redis_request, queue);

        assert(req->write_cb);
        wrote = req->write_cb(req, n);
        if (wrote == 0) {
            if (redis__request_queue_pop_wait_write(self) == NULL) {
                return -1;
            }

            continue;
        }

        assert(wrote <= n);
        n -= wrote;
    }

    assert(n == 0);
    return 0;
}

int redis_request_queue_read_cb(redis_request_queue *self, const char *buf, size_t len) {
    ngx_queue_t *q;
    redis_request *req;
    redis_protocol *p;
    size_t nparsed;

    while (len) {
        p = NULL;
        nparsed = redis_parser_execute(&self->parser, &p, buf, len);

        /* Test for parse error */
        if (nparsed < len && p == NULL) {
            errno = redis_parser_err(&self->parser);
            return REDIS_EPARSER;
        }

        if (!ngx_queue_empty(&self->request_wait_read)) {
            q = ngx_queue_last(&self->request_wait_read);
            req = ngx_queue_data(q, redis_request, queue);

            /* Fire raw read callback when defined */
            if (req->read_raw_cb) {
                req->read_raw_cb(req, buf, nparsed);
            }

            /* Fire read callback when the parser produced something */
            if (p != NULL) {
                int done = 0;

                req->read_cb(req, p, &done);
                if (done) {
                    ngx_queue_remove(q);
                    req->free(req);
                }
            }
        } else {
            assert(NULL && "todo");
        }

        buf += nparsed;
        len -= nparsed;
    }

    return REDIS_OK;
}
