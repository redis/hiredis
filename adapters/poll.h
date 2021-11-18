
#ifndef HIREDIS_POLL_H
#define HIREDIS_POLL_H

#include "../async.h"
#include <string.h> // for memset

/* Values to return from redisPollTick */
#define REDIS_POLL_HANDLED_READ    1
#define REDIS_POLL_HANDLED_WRITE   2
#define REDIS_POLL_HANDLED_TIMEOUT 4

/* An adapter to allow manual polling of the async context by checking the state
 * of the underlying file descriptor.  Useful in cases where there is no formal
 * IO event loop but regular ticking can be used, such as in game engines. */

typedef struct redisPollEvents {
    redisAsyncContext *context;
    redisFD fd;
    char reading, writing;
    char in_tick;
    char deleted;
    double deadline;
} redisPollEvents;

static void redisPollDoubleToTimeval(struct timeval *dst, double src) {
#ifdef _MSC_VER
        dst->tv_sec = (long)src;
        dst->tv_usec = (long)((src-(int)src) * 1e6);
#else
        dst->tv_sec = src;
        dst->tv_usec = (src - (double)dst->tv_sec) * 1000000.00 + .5;
#endif
}

static double redisPollTimevalToDouble(struct timeval *tv) {
    if (tv == NULL)
        return 0.0;
    return tv->tv_sec + tv->tv_usec / 1000000.00;
}

static double redisPollGetNow(void) {
#ifndef _MSC_VER
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return redisPollTimevalToDouble(&tv);
#else
    FILETIME ft;
    ULARGE_INTEGER li;
    GetSystemTimeAsFileTime(&ft);
    li.HighPart = ft.dwHighDateTime;
    li.LowPart = ft.dwLowDateTime;
    return (double)li.QuadPart * 1e-7;
#endif
}

/* Poll for io, handling any pending callbacks.  The timeout argument can be
 * positive to wait for a maximum given time for IO, zero to poll, or negative
 * to wait forever */
static int redisPollTick(redisAsyncContext *ac, double timeout) {
    struct timeval tv_timeout = { 0 };
    struct timeval *ptimeout = &tv_timeout;
    int reading, writing;
    fd_set sr, sw, se;
    int handled;
    redisFD fd;
    int ns;

    redisPollEvents *e = (redisPollEvents*)ac->ev.data;
    if (!e)
        return 0;

    /* local flags, won't get changed during callbacks */
    reading = e->reading;
    writing = e->writing;
    if (!reading && !writing)
        return 0;

    fd = e->fd;

    if (reading) {
        FD_ZERO(&sr);
        FD_SET(fd, &sr);
    }

    if (writing) {
        /* on Windows, connection failure is indicated with the Exception fdset.
         * handle it the same as writable. */
        FD_ZERO(&sw);
        FD_SET(fd, &sw);
        FD_ZERO(&se);
        FD_SET(fd, &se);
    }

    if (timeout > 0.0) {
        redisPollDoubleToTimeval(&tv_timeout, timeout);
    } else if (timeout < 0.0) {
        ptimeout = NULL;
    }

    /* first argument is ignored on windows, therefore it is safe to cast to int */
    ns = select((int)fd + 1, reading ? &sr : NULL, writing ? &sw : NULL, writing ? &se : NULL, ptimeout);
    handled = 0;
    e->in_tick = 1;
    if (ns) {
        if (reading && FD_ISSET(fd, &sr)) {
            redisAsyncHandleRead(ac);
            handled |= REDIS_POLL_HANDLED_READ;
        }
        if (writing && (FD_ISSET(fd, &sw) || FD_ISSET(fd, &se))) {
            /* context Read callback may have caused context to be deleted, e.g.
               by doing an redisAsyncDisconnect() */
            if (!e->deleted) {
                redisAsyncHandleWrite(ac);
                handled |= REDIS_POLL_HANDLED_WRITE;
            }
        }
    }

    /* perform timeouts */
    if (!e->deleted && e->deadline != 0.0) {
        double now = redisPollGetNow();
        if (now >= e->deadline) {
            /* deadline has passed.  disable timeout and perform callback */
            e->deadline = 0.0;
            redisAsyncHandleTimeout(ac);
            handled |= REDIS_POLL_HANDLED_TIMEOUT;
        }
    }

    /* do a delayed cleanup if required */
    if (e->deleted)
        hi_free(e);
    else
        e->in_tick = 0;

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

static void redisPollScheduleTimer(void *data, struct timeval tv)
{
    redisPollEvents *e = (redisPollEvents*)data;
    double now = redisPollGetNow();
    e->deadline = now + redisPollTimevalToDouble(&tv);
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
    memset(e, 0, sizeof(*e));

    e->context = ac;
    e->fd = c->fd;
    e->reading = e->writing = 0;
    e->in_tick = e->deleted = 0;
    e->deadline = 0.0;

    /* Register functions to start/stop listening for events */
    ac->ev.addRead = redisPollAddRead;
    ac->ev.delRead = redisPollDelRead;
    ac->ev.addWrite = redisPollAddWrite;
    ac->ev.delWrite = redisPollDelWrite;
    ac->ev.scheduleTimer = redisPollScheduleTimer;
    ac->ev.cleanup = redisPollCleanup;
    ac->ev.data = e;

    return REDIS_OK;
}
#endif /* HIREDIS_POLL_H */
