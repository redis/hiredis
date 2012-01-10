#ifndef HIREDIS_REQUEST_H
#define HIREDIS_REQUEST_H 1

#include <stddef.h> /* offsetof */
#include "ngx-queue.h"
#include "parser.h"

typedef struct redis_request_s redis_request;

/*
 * Obtain a char* to a buffer representing (some part of) the request.
 *
 * Arguments:
 *  self      the request as previously inserted in the queue
 *  buf       buffer to write, or NULL when there are no more
 *  len       length of the buffer to write
 */
typedef void (redis_request_write_ptr)(redis_request *self,
                                       const char **buf,
                                       size_t *len);

/*
 * Let the request know that (some part of) it has been written.
 *
 * Arguments:
 *  self      the request as previously inserted in the queue
 *  n         the number of bytes written
 *
 * Return:
 *  the number of bytes (< n) that could be accounted for
 */
typedef int (redis_request_write_cb)(redis_request *self,
                                     int n);

/*
 * Let the request know the wire-level data that was fed to the parser on its
 * behalf. This is merely a convenience function that can be used to buffer
 * the wire-level representation of the response, for example.
 *
 * Arguments:
 *  self      the request as previously inserted in the queue
 *  buf       buffer with reply data
 *  len       length of buffer with reply data
 */
typedef void (redis_request_read_raw_cb)(redis_request *self,
                                         const char *buf,
                                         size_t len);

/*
 * Let the request know that a full reply was read on its behalf. This
 * function is called when the protocol parser parsed a full reply and this
 * request is on the head of the list of requests waiting for a reply.
 *
 * Arguments:
 *  self      the request as previously inserted in the queue
 *  reply     reply as read by the parser
 *  done      set to non-zero by the request when this is the last reply
 */
typedef void (redis_request_read_cb)(redis_request *self,
                                     redis_protocol *reply,
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
    ngx_queue_t queue;                                                        \
    redis_request_write_ptr *write_ptr;                                       \
    redis_request_write_cb *write_cb;                                         \
    redis_request_read_raw_cb *read_raw_cb;                                   \
    redis_request_read_cb *read_cb;                                           \
    redis_request_free *free;

struct redis_request_s {
    REDIS_REQUEST_COMMON
};

typedef struct redis_request_queue_s redis_request_queue;

/*
 * These functions are called whenever a request is inserted in a new queue.
 * When a request is initially inserted via the `redis_request_queue_insert`
 * function, it ends up in the `to_write` queue and the `to_write_cb` callback
 * is called. Before the request emits one or more pointers to its buffers, it
 * is placed in the `wait_write` queue and the `wait_write_cb` callback is
 * called. Finally, when the request is fully put on the wire and its
 * `write_cb` function has indicated that it is done, the request is placed in
 * the `wait_read` queue and the `wait_read_cb` callback is called.
 *
 * Arguments:
 *  self      request queue
 *  request   request that was moved to a new queue
 */
typedef void (request_queue_to_write_cb)(redis_request_queue *self,
                                         redis_request *request);
typedef void (request_queue_wait_write_cb)(redis_request_queue *self,
                                           redis_request *request);
typedef void (request_queue_wait_read_cb)(redis_request_queue *self,
                                          redis_request *request);

struct redis_request_queue_s {
    ngx_queue_t request_to_write;
    ngx_queue_t request_wait_write;
    ngx_queue_t request_wait_read;
    redis_parser parser;

    request_queue_to_write_cb *request_to_write_cb;
    request_queue_wait_write_cb *request_wait_write_cb;
    request_queue_wait_read_cb *request_wait_read_cb;

    void *data;
};

int redis_request_init(redis_request *self);
int redis_request_destroy(redis_request *self);

int redis_request_queue_init(redis_request_queue *self);
int redis_request_queue_destroy(redis_request_queue *self);

void redis_request_queue_insert(redis_request_queue *self, redis_request *request);
int redis_request_queue_write_ptr(redis_request_queue *self, const char **buf, size_t *len);
int redis_request_queue_write_cb(redis_request_queue *self, int n);
int redis_request_queue_read_cb(redis_request_queue *self, const char *buf, size_t len);

#endif
