#ifndef __HIREDIS_LIBHV_H__
#define __HIREDIS_LIBHV_H__

#include <hv/hloop.h>
#include "../hiredis.h"
#include "../async.h"

static void redisLibhvHandleEvents(hio_t* io) {
    redisAsyncContext* context = (redisAsyncContext*)hevent_userdata(io);
    int events = hio_events(io);
    int revents = hio_revents(io);
    if (context && (events & HV_READ) && (revents & HV_READ)) {
        redisAsyncHandleRead(context);
    }
    if (context && (events & HV_WRITE) && (revents & HV_WRITE)) {
        redisAsyncHandleWrite(context);
    }
}

static void redisLibhvAddRead(void *privdata) {
    hio_t* io = (hio_t*)privdata;
    hio_add(io, redisLibhvHandleEvents, HV_READ);
}

static void redisLibhvDelRead(void *privdata) {
    hio_t* io = (hio_t*)privdata;
    hio_del(io, HV_READ);
}

static void redisLibhvAddWrite(void *privdata) {
    hio_t* io = (hio_t*)privdata;
    hio_add(io, redisLibhvHandleEvents, HV_WRITE);
}

static void redisLibhvDelWrite(void *privdata) {
    hio_t* io = (hio_t*)privdata;
    hio_del(io, HV_WRITE);
}

static void redisLibhvCleanup(void *privdata) {
    hio_t* io = (hio_t*)privdata;
    hio_close(io);
    hevent_set_userdata(io, NULL);
}

static void redisLibhvTimeout(htimer_t* timer) {
    hio_t* io = (hio_t*)hevent_userdata(timer);
    redisAsyncHandleTimeout(hevent_userdata(io));
}

static void redisLibhvSetTimeout(void *privdata, struct timeval tv) {
    hio_t* io = (hio_t*)privdata;
    hloop_t* loop = hevent_loop(io);
    htimer_t *timer;
    uint32_t millis;

    millis = tv.tv_sec * 1000 + tv.tv_usec / 1000;
    timer = htimer_add(loop, redisLibhvTimeout, millis, 1);

    hevent_set_userdata(timer, io);
}

static int redisLibhvAttach(redisAsyncContext* ac, hloop_t* loop) {
    redisContext *c = &(ac->c);
    hio_t* io = NULL;

    if (ac->ev.data != NULL) {
        return REDIS_ERR;
    }

    io = hio_get(loop, c->fd);
    if (io == NULL) {
        return REDIS_ERR;
    }
    hevent_set_userdata(io, ac);

    ac->ev.addRead  = redisLibhvAddRead;
    ac->ev.delRead  = redisLibhvDelRead;
    ac->ev.addWrite = redisLibhvAddWrite;
    ac->ev.delWrite = redisLibhvDelWrite;
    ac->ev.cleanup  = redisLibhvCleanup;
    ac->ev.scheduleTimer = redisLibhvSetTimeout;
    ac->ev.data     = io;

    return REDIS_OK;
}
#endif
