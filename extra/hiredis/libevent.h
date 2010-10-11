#include <sys/types.h>
#include <event.h>
#include <hiredis.h>

/* Prototype for the error callback. */
typedef void (redisErrorCallback)(redisContext*);

/* This struct enables us to pass both the events and the
 * redisContext to the read and write handlers. */
typedef struct redisEvents {
    redisContext *context;
    redisErrorCallback *err;
    struct event rev, wev;
} redisEvents;

void redisLibEventRead(int fd, short event, void *arg) {
    ((void)fd); ((void)event);
    redisEvents *e = arg;

    /* Always re-schedule read events */
    event_add(&e->rev,NULL);

    if (redisBufferRead(e->context) == REDIS_ERR) {
        /* Handle error. */
        e->err(e->context);
    } else {
        /* If processing the replies/callbacks results in an error,
         * invoke the error callback and abort. */
        if (redisProcessCallbacks(e->context) == REDIS_ERR) {
            e->err(e->context);
        }
    }
}

void redisLibEventWrite(int fd, short event, void *arg) {
    ((void)fd); ((void)event);
    redisEvents *e = arg;
    int done = 0;

    if (redisBufferWrite(e->context, &done) == REDIS_ERR) {
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
void redisLibEventOnWrite(redisContext *c, void *privdata) {
    ((void)c);
    redisEvents *e = privdata;
    event_add(&e->wev,NULL);
}

/* Remove event handlers when the context gets disconnected. */
void redisLibEventOnDisconnect(redisContext *c, void *privdata) {
    ((void)c);
    redisEvents *e = privdata;
    event_del(&e->rev);
    event_del(&e->wev);
}

/* Free the redisEvents struct when the context is free'd. */
void redisLibEventOnFree(redisContext *c, void *privdata) {
    ((void)c);
    redisEvents *e = privdata;
    free(e);
}

redisContext *redisLibEventConnect(const char *ip, int port, redisErrorCallback *err, struct event_base *base) {
    redisEvents *e;
    redisContext *c = redisConnectNonBlock(ip, port, NULL);
    if (c->error != NULL) {
        err(c);
        return NULL;
    }

    /* Create container for context and r/w events */
    e = malloc(sizeof(*e));
    e->context = c;
    e->err = err;

    /* Register callbacks and events */
    redisSetDisconnectCallback(e->context, redisLibEventOnDisconnect, e);
    redisSetCommandCallback(e->context, redisLibEventOnWrite, e);
    redisSetFreeCallback(e->context, redisLibEventOnFree, e);
    event_set(&e->rev, e->context->fd, EV_READ, redisLibEventRead, e);
    event_set(&e->wev, e->context->fd, EV_WRITE, redisLibEventWrite, e);
    event_base_set(base, &e->rev);
    event_base_set(base, &e->wev);
    return c;
}
