#ifndef __HIREDIS_REDISMODULEAPI_H__
#define __HIREDIS_REDISMODULEAPI_H__

#include "redismodule.h"

#include "../async.h"
#include "../hiredis.h"

#include <sys/types.h>

typedef struct redisModuleEvents {
    redisAsyncContext *context;
    int fd;
    int reading, writing;
} redisModuleEvents;

static void redisModuleReadEvent(int fd, void *privdata, int mask) {
    (void) fd;
    (void) mask;

    redisModuleEvents *e = (redisModuleEvents*)privdata;
    redisAsyncHandleRead(e->context);
}

static void redisModuleWriteEvent(int fd, void *privdata, int mask) {
    (void) fd;
    (void) mask;

    redisModuleEvents *e = (redisModuleEvents*)privdata;
    redisAsyncHandleWrite(e->context);
}

static void redisModuleAddRead(void *privdata) {
    redisModuleEvents *e = (redisModuleEvents*)privdata;
    if (!e->reading) {
        e->reading = 1;
        RedisModule_EventLoopAdd(e->fd, REDISMODULE_EVENTLOOP_READABLE, redisModuleReadEvent, e);
    }
}

static void redisModuleDelRead(void *privdata) {
    redisModuleEvents *e = (redisModuleEvents*)privdata;
    if (e->reading) {
        e->reading = 0;
        RedisModule_EventLoopDel(e->fd, REDISMODULE_EVENTLOOP_READABLE);
    }
}

static void redisModuleAddWrite(void *privdata) {
    redisModuleEvents *e = (redisModuleEvents*)privdata;
    if (!e->writing) {
        e->writing = 1;
        RedisModule_EventLoopAdd(e->fd, REDISMODULE_EVENTLOOP_WRITABLE, redisModuleWriteEvent, e);
    }
}

static void redisModuleDelWrite(void *privdata) {
    redisModuleEvents *e = (redisModuleEvents*)privdata;
    if (e->writing) {
        e->writing = 0;
        RedisModule_EventLoopDel(e->fd, REDISMODULE_EVENTLOOP_WRITABLE);
    }
}

static void redisModuleCleanup(void *privdata) {
    redisModuleEvents *e = (redisModuleEvents*)privdata;
    redisModuleDelRead(privdata);
    redisModuleDelWrite(privdata);
    hi_free(e);
}

static int redisModuleAttach(redisAsyncContext *ac) {
    redisContext *c = &(ac->c);
    redisModuleEvents *e;

    /* Nothing should be attached when something is already attached */
    if (ac->ev.data != NULL)
        return REDIS_ERR;

    /* Create container for context and r/w events */
    e = (redisModuleEvents*)hi_malloc(sizeof(*e));
    if (e == NULL)
        return REDIS_ERR;

    e->context = ac;
    e->fd = c->fd;
    e->reading = e->writing = 0;

    /* Register functions to start/stop listening for events */
    ac->ev.addRead = redisModuleAddRead;
    ac->ev.delRead = redisModuleDelRead;
    ac->ev.addWrite = redisModuleAddWrite;
    ac->ev.delWrite = redisModuleDelWrite;
    ac->ev.cleanup = redisModuleCleanup;
    ac->ev.data = e;

    return REDIS_OK;
}

#endif
