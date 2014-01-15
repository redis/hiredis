#ifndef __HIREDIS_LIBUV_H__
#define __HIREDIS_LIBUV_H__
#include <uv.h>
#include "../hiredis.h"
#include "../async.h"


typedef struct redisLibuvEvents {
  redisAsyncContext* context;
  uv_poll_t          handle;
  int                events;
} redisLibuvEvents;


static void redisLibuvPoll(uv_poll_t* handle, int status, int events);


static void redisLibuvAddRead(void *privdata);


static void redisLibuvDelRead(void *privdata);


static void redisLibuvAddWrite(void *privdata);


static void redisLibuvDelWrite(void *privdata);


static void on_close(uv_handle_t* handle);


static void redisLibuvCleanup(void *privdata);


int redisLibuvAttach(redisAsyncContext* ac, uv_loop_t* loop);
#endif
