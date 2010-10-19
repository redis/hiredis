#include <sys/types.h>
#include <event.h>
#include <hiredis.h>

/* Prototype for the error callback. */
typedef void (redisErrorCallback)(const redisContext*);

typedef struct libeventRedisEvents {
    redisContext *context;
    redisErrorCallback *err;
    struct event rev, wev;
} libeventRedisEvents;

void libeventRedisReadEvent(int fd, short event, void *arg) {
    ((void)fd); ((void)event);
    libeventRedisEvents *e = arg;

    /* Always re-schedule read events */
    event_add(&e->rev,NULL);

    if (redisBufferRead(e->context) == REDIS_ERR) {
        e->err(e->context);
    } else {
        if (redisProcessCallbacks(e->context) == REDIS_ERR) {
            e->err(e->context);
        }
    }
}

void libeventRedisWriteEvent(int fd, short event, void *arg) {
    ((void)fd); ((void)event);
    libeventRedisEvents *e = arg;
    int done = 0;

    if (redisBufferWrite(e->context,&done) == REDIS_ERR) {
        /* Handle error */
        e->err(e->context);
    } else {
        /* Schedule write event again when writing is not done. */
        if (!done) {
            event_add(&e->wev,NULL);
        } else {
            event_add(&e->rev,NULL);
        }
    }
}

/* Schedule to be notified on a write event, so the outgoing buffer
 * can be flushed to the socket. */
void libeventRedisCommandCallback(redisContext *c, void *privdata) {
    ((void)c);
    libeventRedisEvents *e = privdata;
    event_add(&e->wev,NULL);
}

/* Remove event handlers when the context gets disconnected. */
void libeventRedisDisconnectCallback(redisContext *c, void *privdata) {
    ((void)c);
    libeventRedisEvents *e = privdata;
    event_del(&e->rev);
    event_del(&e->wev);
}

/* Free the libeventRedisEvents struct when the context is free'd. */
void libeventRedisFreeCallback(redisContext *c, void *privdata) {
    ((void)c);
    libeventRedisEvents *e = privdata;
    free(e);
}

redisContext *libeventRedisConnect(struct event_base *base, redisErrorCallback *err, const char *ip, int port) {
    libeventRedisEvents *e;
    redisContext *c = redisConnectNonBlock(ip,port,NULL);
    if (c->error != NULL) {
        err(c);
        redisFree(c);
        return NULL;
    }

    /* Create container for context and r/w events */
    e = malloc(sizeof(*e));
    e->context = c;
    e->err = err;

    /* Register callbacks */
    redisSetDisconnectCallback(c,libeventRedisDisconnectCallback,e);
    redisSetCommandCallback(c,libeventRedisCommandCallback,e);
    redisSetFreeCallback(c,libeventRedisFreeCallback,e);

    /* Initialize and install read/write events */
    event_set(&e->rev,c->fd,EV_READ,libeventRedisReadEvent,e);
    event_set(&e->wev,c->fd,EV_WRITE,libeventRedisWriteEvent,e);
    event_base_set(base,&e->rev);
    event_base_set(base,&e->wev);
    return c;
}
