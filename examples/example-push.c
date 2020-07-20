#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <hiredis.h>
#include <win32.h>

#define KEY_COUNT 5

#define panicAbort(fmt, ...) \
    do { \
        fprintf(stderr, "%s:%d:%s(): " fmt, __FILE__, __LINE__, __func__, __VA_ARGS__); \
        exit(-1); \
    } while (0)

static void assertReplyAndFree(redisContext *context, redisReply *reply, int type) {
    if (reply == NULL)
        panicAbort("NULL reply from server (error: %s)", context->errstr);

    if (reply->type != type) {
        if (reply->type == REDIS_REPLY_ERROR)
            fprintf(stderr, "Redis Error: %s\n", reply->str);

        panicAbort("Expected reply type %d but got type %d", type, reply->type);
    }

    freeReplyObject(reply);
}

/* Switch to the RESP3 protocol and enable client tracking */
static void enableClientTracking(redisContext *c) {
    redisReply *reply = redisCommand(c, "HELLO 3");
    if (reply == NULL || c->err) {
        panicAbort("NULL reply or server error (error: %s)", c->errstr);
    }

    if (reply->type != REDIS_REPLY_MAP) {
        fprintf(stderr, "Error: Can't send HELLO 3 command.  Are you sure you're ");
        fprintf(stderr, "connected to redis-server >= 6.0.0?\nRedis error: %s\n",
                        reply->type == REDIS_REPLY_ERROR ? reply->str : "(unknown)");
        exit(-1);
    }

    freeReplyObject(reply);

    /* Enable client tracking */
    reply = redisCommand(c, "CLIENT TRACKING ON");
    assertReplyAndFree(c, reply, REDIS_REPLY_STATUS);
}

void pushReplyHandler(void *privdata, void *r) {
    redisReply *reply = r;
    int *invalidations = privdata;

    /* Sanity check on the invalidation reply */
    if (reply->type != REDIS_REPLY_PUSH || reply->elements != 2 ||
        reply->element[1]->type != REDIS_REPLY_ARRAY ||
        reply->element[1]->element[0]->type != REDIS_REPLY_STRING)
    {
        panicAbort("%s", "Can't parse PUSH message!");
    }

    /* Increment our invalidation count */
    *invalidations += 1;

    printf("pushReplyHandler(): INVALIDATE '%s' (invalidation count: %d)\n",
           reply->element[1]->element[0]->str, *invalidations);

    freeReplyObject(reply);
}

int main(int argc, char **argv) {
    unsigned int j, invalidations = 0;
    redisContext *c;
    redisReply *reply;

    const char *hostname = (argc > 1) ? argv[1] : "127.0.0.1";
    int port = (argc > 2) ? atoi(argv[2]) : 6379;

    redisOptions o = {0};
    REDIS_OPTIONS_SET_TCP(&o, hostname, port);

    /* Set our custom PUSH message handler */
    o.push_cb = pushReplyHandler;

    c = redisConnectWithOptions(&o);
    if (c == NULL || c->err)
        panicAbort("Connection error:  %s", c ? c->errstr : "OOM");

    /* Enable RESP3 and turn on client tracking */
    enableClientTracking(c);

    /* Set our context privdata to the address of our invalidation counter.  Each
     * time our PUSH handler is called, hiredis will pass the privdata for context */
    c->privdata = &invalidations;

    /* Set some keys and then read them back.  Once we do that, Redis will deliver
     * invalidation push messages whenever the key is modified */
    for (j = 0; j < KEY_COUNT; j++) {
        reply = redisCommand(c, "SET key:%d initial:%d", j, j);
        assertReplyAndFree(c, reply, REDIS_REPLY_STATUS);

        reply = redisCommand(c, "GET key:%d", j);
        assertReplyAndFree(c, reply, REDIS_REPLY_STRING);
    }

    /* Trigger invalidation messages by updating keys we just read */
    for (j = 0; j < KEY_COUNT; j++) {
        printf("            main(): SET key:%d update:%d\n", j, j);
        reply = redisCommand(c, "SET key:%d update:%d", j, j);
        assertReplyAndFree(c, reply, REDIS_REPLY_STATUS);
        printf("            main(): SET REPLY OK\n");
    }

    printf("\nTotal detected invalidations: %d, expected: %d\n", invalidations, KEY_COUNT);

    /* PING server */
    redisFree(c);
}
