#include <stdio.h>
#include <stdlib.h>
#include <event.h>
#include <string.h>
#include "hiredis.h"

#define NOT_USED(x) ((void)x)

/* This struct enables us to pass both the event and the
 * redisContext to the read and write handlers. */
typedef struct redisEvents {
    redisContext *context;
    struct event rev, wev;
} redisEvents;

void redisLibEventRead(int fd, short event, void *arg) {
    NOT_USED(fd); NOT_USED(event);
    redisEvents *e = arg;

    /* Always re-schedule read events */
    event_add(&e->rev,NULL);

    if (redisBufferRead(e->context) == REDIS_ERR) {
        /* Handle error. */
        printf("Read error: %s\n", e->context->error);
    } else {
        /* Check replies. */
        redisProcessCallbacks(e->context);
    }
}

void redisLibEventWrite(int fd, short event, void *arg) {
    NOT_USED(fd); NOT_USED(event);
    redisEvents *e = arg;
    int done = 0;

    if (redisBufferWrite(e->context, &done) == REDIS_ERR) {
        /* Handle error */
        printf("Write error: %s\n", e->context->error);
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
    NOT_USED(c);
    redisEvents *e = privdata;
    event_add(&e->wev,NULL);
}

/* Free the redisEvents struct when the context is free'd. */
void redisLibEventOnFree(redisContext *c, void *privdata) {
    NOT_USED(c);
    redisEvents *e = privdata;
    free(e);
}

redisContext *redisLibEventConnect(const char *ip, int port) {
    redisEvents *e = malloc(sizeof(*e));
    e->context = redisConnectNonBlock(ip, port, NULL);
    redisSetCommandCallback(e->context, redisLibEventOnWrite, e);
    redisSetFreeCallback(e->context, redisLibEventOnFree, e);
    event_set(&e->rev, e->context->fd, EV_READ, redisLibEventRead, e);
    event_set(&e->wev, e->context->fd, EV_WRITE, redisLibEventWrite, e);
    return e->context;
}

void getCallback(redisContext *c, redisReply *reply, void *privdata) {
    NOT_USED(c); NOT_USED(privdata);
    printf("argv[end-1]: %s\n", reply->reply);
    redisFree(c);
    exit(0);
}

int main (int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
    event_init();

    redisContext *c = redisLibEventConnect("127.0.0.1", 6379);
    if (c->error != NULL) {
        printf("Connect error: %s\n", c->error);
        return 1;
    }

    redisCommand(c, "SET key %b", argv[argc-1], strlen(argv[argc-1]));
    redisCommandWithCallback(c, getCallback, NULL, "GET key");
    event_dispatch();
    return 0;
}
