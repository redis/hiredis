#ifndef __HIREDIS_LIBUBOX_H__
#define __HIREDIS_LIBUBOX_H__
#include <uloop.h>
#include "../hiredis.h"
#include "../async.h"

typedef struct redisLibuboxEvents {
    redisAsyncContext *context;
    struct uloop_fd fd;
} redisLibuboxEvents;



static void redisLibuboxEvent(struct uloop_fd *fd, unsigned int events)
{
    redisLibuboxEvents *e = container_of(fd,redisLibuboxEvents,fd);
	redisAsyncContext *context = e->context; 
	
	if(events & ULOOP_READ){
		redisAsyncHandleRead(context);
	}
	else if(events & ULOOP_WRITE){
		redisAsyncHandleWrite(context);
	}
}

static void redisLibuboxAddRead(void *privdata) {
    redisLibuboxEvents *e = (redisLibuboxEvents*)privdata;
    e->fd.cb = redisLibuboxEvent;
    uloop_fd_add(&e->fd, ULOOP_READ);
}

static void redisLibuboxDelRead(void *privdata) {
    redisLibuboxEvents *e = (redisLibuboxEvents*)privdata;
    uloop_fd_add(&e->fd, e->fd.flags & ~ULOOP_READ);
}

static void redisLibuboxAddWrite(void *privdata) {
    redisLibuboxEvents *e = (redisLibuboxEvents*)privdata;
    e->fd.cb = redisLibuboxEvent;
    uloop_fd_add(&e->fd, ULOOP_WRITE);
}

static void redisLibuboxDelWrite(void *privdata) {
    redisLibuboxEvents *e = (redisLibuboxEvents*)privdata;
    uloop_fd_add(&e->fd, e->fd.flags & ~ULOOP_WRITE);
}

static void redisLibuboxCleanup(void *privdata) {
    redisLibuboxEvents *e = (redisLibuboxEvents*)privdata;
    uloop_fd_delete(&e->fd);
    free(e);
}

static int redisLibuboxAttach(redisAsyncContext *ac) {
    redisContext *c = &(ac->c);
    redisLibuboxEvents *e;

    /* Nothing should be attached when something is already attached */
    if (ac->ev.data != NULL)
        return REDIS_ERR;

    /* Create container for context and r/w events */
    e = (redisLibuboxEvents*)hi_calloc(1,sizeof(*e));
    e->context = ac;

    /* Register functions to start/stop listening for events */
    ac->ev.addRead = redisLibuboxAddRead;
    ac->ev.delRead = redisLibuboxDelRead;
    ac->ev.addWrite = redisLibuboxAddWrite;
    ac->ev.delWrite = redisLibuboxDelWrite;
    ac->ev.cleanup = redisLibuboxCleanup;
    ac->ev.data = e;

    /* Initialize and install read/write events */
    e->fd.fd = c->fd;
    e->fd.cb = redisLibuboxEvent;
    uloop_fd_add(&e->fd, ULOOP_READ | ULOOP_WRITE);
 
    return REDIS_OK;
}
#endif
