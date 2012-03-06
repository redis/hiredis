#include <string.h>
#include <assert.h>
#include <errno.h>

#include "handle.h" /* return values */
#include "request.h"
#include "object.h" /* default parser callbacks */

int redis_request_init(redis_request *self) {
    memset(self, 0, sizeof(*self));
    ngx_queue_init(&self->wq);
    ngx_queue_init(&self->rq);
    return REDIS_OK;
}

void redis_request_destroy(redis_request *self) {
    ngx_queue_remove(&self->rq);
    ngx_queue_remove(&self->wq);
    self->free(self);
}

int redis_request_queue_init(redis_request_queue *self) {
    memset(self, 0, sizeof(*self));
    ngx_queue_init(&self->request_to_write);
    ngx_queue_init(&self->request_wait_write);
    ngx_queue_init(&self->request_wait_read);
    redis_parser_init(&self->parser, &redis_object_parser_callbacks);
    return REDIS_OK;
}

int redis_request_queue_destroy(redis_request_queue *self) {
    ngx_queue_t *q;
    redis_request *req;

    while (!ngx_queue_empty(&self->request_wait_read)) {
        q = ngx_queue_last(&self->request_wait_read);
        req = ngx_queue_data(q, redis_request, rq);
        redis_request_destroy(req);
    }

    while (!ngx_queue_empty(&self->request_wait_write)) {
        q = ngx_queue_last(&self->request_wait_write);
        req = ngx_queue_data(q, redis_request, wq);
        redis_request_destroy(req);
    }

    while (!ngx_queue_empty(&self->request_to_write)) {
        q = ngx_queue_last(&self->request_to_write);
        req = ngx_queue_data(q, redis_request, wq);
        redis_request_destroy(req);
    }

    redis_parser_destroy(&self->parser);
    return REDIS_OK;
}

void redis_request_queue_write(redis_request_queue *self) {
    if (self->write_fn) {
        self->write_fn(self);
    }
}

void redis_request_queue_start_read(redis_request_queue *self) {
    if (self->start_read_fn) {
        self->start_read_fn(self);
    }
}

void redis_request_queue_stop_read(redis_request_queue *self) {
    if (self->stop_read_fn) {
        self->stop_read_fn(self);
    }
}

void redis_request_queue_insert(redis_request_queue *self, redis_request *request) {
    request->request_queue = self;
    ngx_queue_insert_head(&self->request_to_write, &request->wq);

    if (self->request_to_write_cb) {
        self->request_to_write_cb(self, request);
    }

    /* Kickstart writes when this is the first pending write */
    self->pending_writes++;
    if (self->pending_writes == 1) {
        redis_request_queue_write(self);
    }
}

int redis_request_queue_write_ptr(redis_request_queue *self, const char **dstbuf, size_t *dstlen) {
    ngx_queue_t *q = NULL;
    redis_request *req = NULL;
    const char *buf = NULL;
    size_t len = 0;
    int done = 0;

    if (!ngx_queue_empty(&self->request_wait_write)) {
        q = ngx_queue_head(&self->request_wait_write);
        req = ngx_queue_data(q, redis_request, wq);
    }

    /* Continue until write_ptr returned a non-empty buffer */
    while (len == 0) {

        /* Make sure that `req` is assigned a non-done request */
        if (req == NULL || req->write_ptr_done) {
            if (ngx_queue_empty(&self->request_to_write)) {
                return -1;
            }

            q = ngx_queue_last(&self->request_to_write);
            req = ngx_queue_data(q, redis_request, wq);

            /* Remove from tail of `to_write`, insert on head of `wait_write` */
            ngx_queue_remove(q);
            ngx_queue_insert_head(&self->request_wait_write, q);

            if (self->request_wait_write_cb) {
                self->request_wait_write_cb(self, req);
            }
        }

        buf = NULL;
        len = 0;
        done = 0;

        assert(req->write_ptr);
        req->write_ptr(req, &buf, &len, &done);

        if (done) {
            req->write_ptr_done = 1;
            self->pending_writes--;
        }
    }

    *dstbuf = buf;
    *dstlen = len;

    return 0;
}

int redis_request_queue_write_cb(redis_request_queue *self, size_t len) {
    ngx_queue_t *q = NULL;
    redis_request *req = NULL;
    int nwritten, done;

    while (len) {
        if (ngx_queue_empty(&self->request_wait_write)) {
            return -1;
        }

        q = ngx_queue_last(&self->request_wait_write);
        req = ngx_queue_data(q, redis_request, wq);
        done = 0;

        /* Add this request to `wait_read` if necessary */
        if (ngx_queue_empty(&req->rq)) {
            ngx_queue_insert_head(&self->request_wait_read, &req->rq);

            if (self->request_wait_read_cb) {
                self->request_wait_read_cb(self, req);
            }
        }

        assert(req->write_cb);
        nwritten = req->write_cb(req, len, &done);

        /* Abort on error */
        if (nwritten < 0) {
            return nwritten;
        }

        assert((unsigned)nwritten <= len);
        len -= nwritten;

        /* Remove this request from `wait_write` when done writing */
        if (done) {
            ngx_queue_remove(q);
            ngx_queue_init(q);
            req->write_cb_done = 1;
        }
    }

    return 0;
}

int redis_request_queue_read_cb(redis_request_queue *self, const char *buf, size_t len) {
    ngx_queue_t *q = NULL;
    redis_request *req = NULL;
    redis_protocol *p = NULL;
    size_t nparsed = 0;
    int done;

    while (len) {
        if (ngx_queue_empty(&self->request_wait_read)) {
            return -1;
        }

        q = ngx_queue_last(&self->request_wait_read);
        req = ngx_queue_data(q, redis_request, rq);
        done = 0;

        nparsed = redis_parser_execute(&self->parser, &p, buf, len);
        if (nparsed < len && p == NULL) {
            errno = redis_parser_err(&self->parser);
            return REDIS_EPARSER;
        }

        assert(req->read_cb);
        req->read_cb(req, p, buf, nparsed, &done);

        /* Request cannot be done on partial reply */
        assert(p || !done);

        /* Update buffer */
        buf += nparsed;
        len -= nparsed;

        /* Remove this request from `wait_read` when done reading */
        if (done) {
            ngx_queue_remove(q);
            ngx_queue_init(q);
            req->read_cb_done = 1;
            redis_request_destroy(req);
        }
    }

    return 0;
}
