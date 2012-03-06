#ifndef HIREDIS_REQUEST_H
#define HIREDIS_REQUEST_H 1

#include <stddef.h> /* offsetof */
#include "ngx-queue.h"
#include "parser.h"

typedef struct redis_request_s redis_request;

typedef struct redis_request_queue_s redis_request_queue;

/*
 * Obtain a char* to a buffer representing (some part of) the request.
 *
 * Arguments:
 *  self      the request as previously inserted in the queue
 *  buf       buffer with request data
 *  len       length of the buffer with request data
 *  done      set to non-zero by the request when it doesn't have more ptrs
 */
typedef void (redis_request_write_ptr)(redis_request *self,
                                       const char **buf,
                                       size_t *len,
                                       int *done);

/*
 * Let the request know that (some part of) it has been written.
 *
 * Arguments:
 *  self      the request as previously inserted in the queue
 *  n         the number of bytes written
 *  done      set to non-zero by the request when it has been written in full
 *
 * Return:
 *  the number of bytes (<= n) actually used
 */
typedef int (redis_request_write_cb)(redis_request *self,
                                     int n,
                                     int *done);

/*
 * Let the request know the wire-level data that was fed to the parser on its
 * behalf. This is merely a convenience function that can be used to buffer
 * the wire-level representation of the response, for example.
 *
 * Arguments:
 *  self      the request as previously inserted in the queue
 *  buf       buffer with reply data
 *  len       length of buffer with reply data
 *  done      set to non-zero by the request when it has been read in full
 *
 * Return:
 *  the number of bytes (<= n) actually used
 */
typedef int (redis_request_read_cb)(redis_request *self,
                                    const char *buf,
                                    size_t len,
                                    int *done);

/*
 * Free the request. This function is called after the last reply has been
 * read, and passed to the request via `read_cb`.

 *
 * Arguments:
 *  self      the request as previously inserted in the queue
 */
typedef void (redis_request_free)(redis_request *self);

#define REDIS_REQUEST_COMMON                                                  \
    redis_request_queue *request_queue;                                       \
    ngx_queue_t wq, rq;                                                       \
    unsigned write_ptr_done:1;                                                \
    unsigned write_cb_done:1;                                                 \
    unsigned read_cb_done:1;                                                  \
    redis_request_write_ptr *write_ptr;                                       \
    redis_request_write_cb *write_cb;                                         \
    redis_request_read_cb *read_cb;                                           \
    redis_request_free *free;

struct redis_request_s {
    REDIS_REQUEST_COMMON
};

/*
 * These functions are called whenever a request is inserted in a new queue.
 * When a request is initially inserted via the `redis_request_queue_insert`
 * function, it ends up in the `to_write` queue and the `to_write_cb` callback
 * is called. Before the request emits one or more pointers to its buffers, it
 * is placed in the `wait_write` queue and the `wait_write_cb` callback is
 * called. Finally, when one or more bytes of the request have been put on the
 * wire, the request is placed in the `wait_read` queue and the `wait_read_cb`
 * callback is called.
 *
 * Arguments:
 *  self      request queue
 *  request   request that was moved to a new queue
 */
typedef void (redis_request_queue_to_write_cb)(redis_request_queue *self,
                                               redis_request *request);
typedef void (redis_request_queue_wait_write_cb)(redis_request_queue *self,
                                                 redis_request *request);
typedef void (redis_request_queue_wait_read_cb)(redis_request_queue *self,
                                                redis_request *request);

/*
 * This type of function is called when I/O needs to happen. All requests that
 * a request queue takes in eventually need to be written to a socket. When a
 * request is inserted while the I/O library was not yet busy draining the
 * queue of requests to write, one of the I/O functions will be called to
 * _kickstart_ writes. After the kickstart the I/O library is supposed to
 * continue writing until it no longer can retrieve `char *`'s to write. After
 * that, it should be kickstarted by user code when user code stopped emitting
 * `char*`'s, or is automatically kickstarted when the queue of requests to
 * write was drained.
 *
 * Arguments:
 *  self      request queue
 */

typedef void (redis_request_queue_io_fn)(redis_request_queue *self);

struct redis_request_queue_s {
    size_t pending_writes;

    ngx_queue_t request_to_write;
    ngx_queue_t request_wait_write;
    ngx_queue_t request_wait_read;
    redis_parser parser;

    redis_request_queue_to_write_cb *request_to_write_cb;
    redis_request_queue_wait_write_cb *request_wait_write_cb;
    redis_request_queue_wait_read_cb *request_wait_read_cb;

    redis_request_queue_io_fn *write_fn;
    redis_request_queue_io_fn *start_read_fn;
    redis_request_queue_io_fn *stop_read_fn;

    void *data;
};

int redis_request_init(redis_request *self);
void redis_request_destroy(redis_request *self);

int redis_request_queue_init(redis_request_queue *self);
int redis_request_queue_destroy(redis_request_queue *self);

void redis_request_queue_start_write(redis_request_queue *self);
void redis_request_queue_start_read(redis_request_queue *self);
void redis_request_queue_stop_read(redis_request_queue *self);

void redis_request_queue_insert(redis_request_queue *self, redis_request *request);
int redis_request_queue_write_ptr(redis_request_queue *self, const char **buf, size_t *len);
int redis_request_queue_write_cb(redis_request_queue *self, size_t len);
int redis_request_queue_read_cb(redis_request_queue *self, const char *buf, size_t len);

#endif
