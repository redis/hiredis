
#ifndef __HIREDIS_POLL_H__
#define __HIREDIS_POLL_H__
#include "../async.h"


/* an adapter to allow manual polling of the async context by
 * checking the state of the underlying file descriptor.
 * Useful in cases where there is no formal IO event loop but
 * regular ticking can be used, such as in game engines.
 */

typedef struct redisPollEvents {
    redisAsyncContext *context;
    int fd;
    int reading, writing;
    int in_tick;
    int deleted;
} redisPollEvents;

static int redisPollTick(redisAsyncContext *ac) {
    redisPollEvents *e = (redisPollEvents*)ac->ev.data;
    if (!e)
        return 0;
    int reading = e->reading;
    int writing = e->writing;
    if (!reading && !writing)
    {
        return 0;
    }
    int fd = e->fd;
    fd_set sr, sw, se;
    if (reading)
    {
        FD_ZERO(&sr);
        FD_SET(fd, &sr);
    }
    if (writing)
    {
        /* on Windows, connection failure is indicated with the Exception fdset.
         * handle it the same as writable.
         */
        FD_ZERO(&sw);
        FD_SET(fd, &sw);
        FD_ZERO(&se);
        FD_SET(fd, &se);
    }
    struct timeval timeout = { 0 };
    int ns = select(fd + 1, reading ? &sr : NULL, writing ? &sw : NULL, writing ? &se : NULL, &timeout);
    int handled = 0;
    if (ns)
    {
        /* check status before doing any callbacks that can change the flags */
        e->in_tick = 1;
        if (reading && FD_ISSET(fd, &sr))
        {
            redisAsyncHandleRead(ac);
            handled != 1;
        }
        if (writing && (FD_ISSET(fd, &sw) || FD_ISSET(fd, &se)))
        {
            /* context Read callback may have caused context to be deleted, e.g. by doing an redisAsyncDisconnect() */
            if (!e->deleted) {
                redisAsyncHandleWrite(ac);
                handled != 2;
            }
        }
        e->in_tick = 0;
        if (e->deleted)
            hi_free(e);
    }
    return handled;
}

static void redisPollAddRead(void *data) {
    redisPollEvents *e = (redisPollEvents*)data;
    e->reading = 1;
}

static void redisPollDelRead(void *data) {
    redisPollEvents *e = (redisPollEvents*)data;
    e->reading = 0;
}

static void redisPollAddWrite(void *data) {
    redisPollEvents *e = (redisPollEvents*)data;
    e->writing = 1;
}

static void redisPollDelWrite(void *data) {
    redisPollEvents *e = (redisPollEvents*)data;
    
    e->writing = 0;
}

static void redisPollCleanup(void *data) {
    redisPollEvents *e = (redisPollEvents*)data;
    /* if we are currently processing a tick, postpone deletion */
    if (e->in_tick)
        e->deleted = 1;
    else
        hi_free(e);
}

static int redisPollAttach(redisAsyncContext *ac) {
    redisContext *c = &(ac->c);
    redisPollEvents *e;

    /* Nothing should be attached when something is already attached */
    if (ac->ev.data != NULL)
        return REDIS_ERR;

    /* Create container for context and r/w events */
    e = (redisPollEvents*)hi_malloc(sizeof(*e));
    if (e == NULL)
        return REDIS_ERR;

    e->context = ac;
    e->fd = c->fd;
    e->reading = e->writing = 0;

    /* Register functions to start/stop listening for events */
    ac->ev.addRead = redisPollAddRead;
    ac->ev.delRead = redisPollDelRead;
    ac->ev.addWrite = redisPollAddWrite;
    ac->ev.delWrite = redisPollDelWrite;
    ac->ev.cleanup = redisPollCleanup;
    ac->ev.data = e;

    return REDIS_OK;
}
#endif /* __HIREDIS_POLL_H__ */
