#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include <assert.h>
#include <hiredis.h>
#include <async.h>
#include <adapters/libevent.h>

void printReplyInternal(const redisReply* r) {
    switch (r->type) {
    case REDIS_REPLY_INTEGER:
        printf("(integer %lld)", r->integer);
        break;
    case REDIS_REPLY_DOUBLE:
        printf("(double %f)", r->dval);
        break;
    case REDIS_REPLY_ERROR:
        printf("(error %s)", r->str);
        break;
    case REDIS_REPLY_STATUS:
        printf("(status %s)", r->str);
        break;
    case REDIS_REPLY_STRING:
        printf("(string %s)", r->str);
        break;
    case REDIS_REPLY_VERB:
        printf("(verb %s)", r->str);
        break;
    case REDIS_REPLY_ARRAY:
        printf("(array %zu", r->elements);
        for (size_t i = 0; i < r->elements; ++i) {
            putchar(' ');
            printReplyInternal(r->element[i]);
        }
        putchar(')');
        break;
    default:
        printf("(?%d)", r->type);
        break;
    }
}

void printReply(const redisReply* r) {
    printReplyInternal(r);
    putchar('\n');
}

void getCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    if (reply == NULL) {
        if (c->errstr) {
            printf("errstr: %s\n", c->errstr);
        }
        return;
    }
    printf("argv[%s]: %s\n", (char*)privdata, reply->str);
}

void getFinalizer(redisAsyncContext *c, void *privdata) {
    printf("Get finalizer called\n");
    redisAsyncDisconnect(c);
}

void connectCallback(const redisAsyncContext *c, int status) {
    if (status != REDIS_OK) {
        printf("Error: %s\n", c->errstr);
        return;
    }
    printf("Connected...\n");
}

void disconnectCallback(const redisAsyncContext *c, int status) {
    if (status != REDIS_OK) {
        printf("Error: %s\n", c->errstr);
        return;
    }
    printf("Disconnected...\n");
}

typedef struct _SubscribeData {
    int break_loop;
    int remaining_message_count;
} SubscribeData;

void subscribeCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    SubscribeData *sd = privdata;
    if (reply == NULL) {
        if (c->errstr) {
            printf("errstr: %s\n", c->errstr);
        }
        return;
    }

    printf("Subscribe reply: ");
    printReply(reply);

    assert(reply->type == REDIS_REPLY_ARRAY);
    assert(reply->elements == 3);
    assert(reply->element[0]->type == REDIS_REPLY_STRING);

    if (!strcasecmp(reply->element[0]->str, "message")) {
        if (--sd->remaining_message_count == 0) {
            redisAsyncCommand(c, NULL, NULL, "UNSUBSCRIBE foo");
        }
    }

    if (sd->break_loop) {
        sd->break_loop = 0;
        redisLibeventEvents* e = c->ev.data;
        event_base_loopbreak(e->base);
    }
}

void subscribeFinalizer(redisAsyncContext *c, void *privdata) {
    printf("Subscribe finalizer called\n");
    redisAsyncDisconnect(c);
}

int main (int argc, char **argv) {
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif

    struct event_base *base = event_base_new();
    redisOptions options = {0};
    REDIS_OPTIONS_SET_TCP(&options, "127.0.0.1", 6379);
    struct timeval tv = {0};
    tv.tv_sec = 1;
    options.connect_timeout = &tv;


    redisAsyncContext *sub = redisAsyncConnectWithOptions(&options);
    if (sub->err) {
        /* Let *c leak for now... */
        printf("Error: %s\n", sub->errstr);
        return 1;
    }

    redisLibeventAttach(sub,base);

    redisAsyncContext *c = redisAsyncConnectWithOptions(&options);
    if (c->err) {
        /* Let *c leak for now... */
        printf("Error: %s\n", c->errstr);
        return 1;
    }

    redisLibeventAttach(c,base);
    redisAsyncSetConnectCallback(c,connectCallback);
    redisAsyncSetDisconnectCallback(c,disconnectCallback);
    
    SubscribeData sd;
    memset(&sd, 0, sizeof(sd));
    sd.break_loop = 1;
    sd.remaining_message_count = 3;
    redisAsyncCommandWithFinalizer(sub, subscribeCallback, subscribeFinalizer, &sd, "SUBSCRIBE foo");
    event_base_dispatch(base);

    redisAsyncCommand(c, NULL, NULL, "SET key %b", argv[argc-1], strlen(argv[argc-1]));
    for (int i = 0; i < 3; ++i) {
        redisAsyncCommand(c, NULL, NULL, "PUBLISH foo msg%d", i);
    }
    redisAsyncCommandWithFinalizer(c, getCallback, getFinalizer, (char*)"end-1", "GET key");
    event_base_dispatch(base);
    return 0;
}
