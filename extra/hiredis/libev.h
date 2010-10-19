#include <sys/types.h>
#include <ev.h>
#include <hiredis.h>

/* Prototype for the error callback. */
typedef void (redisErrorCallback)(const redisContext*);

typedef struct libevRedisEvents {
    redisContext *context;
    redisErrorCallback *err;
    struct ev_loop *loop;
    ev_io rev, wev;
} libevRedisEvents;

void libevRedisReadEvent(struct ev_loop *loop, ev_io *watcher, int revents) {
    ((void)loop); ((void)revents);
    libevRedisEvents *e = watcher->data;

    if (redisBufferRead(e->context) == REDIS_ERR) {
        e->err(e->context);
    } else {
        if (redisProcessCallbacks(e->context) == REDIS_ERR) {
            e->err(e->context);
        }
    }
}

void libevRedisWriteEvent(struct ev_loop *loop, ev_io *watcher, int revents) {
    ((void)loop); ((void)revents);
    libevRedisEvents *e = watcher->data;
    int done = 0;

    if (redisBufferWrite(e->context, &done) == REDIS_ERR) {
        ev_io_stop(e->loop,&e->wev);
        e->err(e->context);
    } else {
        /* Stop firing the write event when done */
        if (done) {
            ev_io_stop(e->loop,&e->wev);
            ev_io_start(e->loop,&e->rev);
        }
    }
}

void libevRedisCommandCallback(redisContext *c, void *privdata) {
    ((void)c);
    libevRedisEvents *e = privdata;
    ev_io_start(e->loop,&e->wev);
}

void libevRedisDisconnectCallback(redisContext *c, void *privdata) {
    ((void)c);
    libevRedisEvents *e = privdata;
    ev_io_stop(e->loop,&e->rev);
    ev_io_stop(e->loop,&e->wev);
}

void libevRedisFreeCallback(redisContext *c, void *privdata) {
    ((void)c);
    libevRedisEvents *e = privdata;
    free(e);
}

redisContext *libevRedisConnect(struct ev_loop *loop, redisErrorCallback *err, const char *ip, int port) {
    libevRedisEvents *e;
    redisContext *c = redisConnectNonBlock(ip, port, NULL);
    if (c->error != NULL) {
        err(c);
        redisFree(c);
        return NULL;
    }

    /* Create container for context and r/w events */
    e = malloc(sizeof(*e));
    e->loop = loop;
    e->context = c;
    e->err = err;
    e->rev.data = e;
    e->wev.data = e;

    /* Register callbacks */
    redisSetDisconnectCallback(c,libevRedisDisconnectCallback,e);
    redisSetCommandCallback(c,libevRedisCommandCallback,e);
    redisSetFreeCallback(c,libevRedisFreeCallback,e);

    /* Initialize read/write events */
    ev_io_init(&e->rev,libevRedisReadEvent,c->fd,EV_READ);
    ev_io_init(&e->wev,libevRedisWriteEvent,c->fd,EV_WRITE);
    return c;
}
