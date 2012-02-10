#ifndef __HIREDIS_LIBEVENT_H__
#define __HIREDIS_LIBEVENT_H__
#include <event.h>
#include <pthread.h>
#include "../hiredis.h"
#include "../async.h"

typedef struct redisLibeventEvents {
    redisAsyncContext *context;
    struct event rev, wev;
    pthread_mutex_t *mutex;
} redisLibeventEvents;

static void redisLibeventReadEvent(int fd, short event, void *arg) {
    ((void)fd); ((void)event);
    redisLibeventEvents *e = (redisLibeventEvents*)arg;
    if (e->mutex) {
        pthread_mutex_lock(e->mutex);
        redisAsyncHandleRead(e->context);
        pthread_mutex_unlock(e->mutex);
    } else {
        redisAsyncHandleRead(e->context);
    }
}

static void redisLibeventWriteEvent(int fd, short event, void *arg) {
    ((void)fd); ((void)event);
    redisLibeventEvents *e = (redisLibeventEvents*)arg;
    if (e->mutex) {
        pthread_mutex_lock(e->mutex);
        redisAsyncHandleWrite(e->context);
        pthread_mutex_unlock(e->mutex);
    } else {
        redisAsyncHandleWrite(e->context);
    }
}

static void redisLibeventAddRead(void *privdata) {
    redisLibeventEvents *e = (redisLibeventEvents*)privdata;
    event_add(&e->rev,NULL);
}

static void redisLibeventDelRead(void *privdata) {
    redisLibeventEvents *e = (redisLibeventEvents*)privdata;
    event_del(&e->rev);
}

static void redisLibeventAddWrite(void *privdata) {
    redisLibeventEvents *e = (redisLibeventEvents*)privdata;
    event_add(&e->wev,NULL);
}

static void redisLibeventDelWrite(void *privdata) {
    redisLibeventEvents *e = (redisLibeventEvents*)privdata;
    event_del(&e->wev);
}

static void redisLibeventCleanup(void *privdata) {
    redisLibeventEvents *e = (redisLibeventEvents*)privdata;
    event_del(&e->rev);
    event_del(&e->wev);
    free(e);
}

static int redisLibeventAttachThreaded(redisAsyncContext *ac, struct event_base *base, pthread_mutex_t *mutex) {
    redisContext *c = &(ac->c);
    redisLibeventEvents *e;

    /* Nothing should be attached when something is already attached */
    if (ac->ev.data != NULL)
        return REDIS_ERR;

    /* Create container for context and r/w events */
    e = (redisLibeventEvents*)malloc(sizeof(*e));
    e->context = ac;
    e->mutex = mutex;

    /* Register functions to start/stop listening for events */
    ac->ev.addRead = redisLibeventAddRead;
    ac->ev.delRead = redisLibeventDelRead;
    ac->ev.addWrite = redisLibeventAddWrite;
    ac->ev.delWrite = redisLibeventDelWrite;
    ac->ev.cleanup = redisLibeventCleanup;
    ac->ev.data = e;

    /* Initialize and install read/write events */
    event_set(&e->rev,c->fd,EV_READ,redisLibeventReadEvent,e);
    event_set(&e->wev,c->fd,EV_WRITE,redisLibeventWriteEvent,e);
    event_base_set(base,&e->rev);
    event_base_set(base,&e->wev);
    return REDIS_OK;
}

#define redisLibeventAttach(ac, base) redisLibeventAttachThreaded(ac, base, NULL)

#endif
