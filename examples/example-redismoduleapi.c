#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include <hiredis.h>
#include <async.h>
#include <adapters/redismoduleapi.h>

void getCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    if (reply == NULL) {
        if (c->errstr) {
            printf("errstr: %s\n", c->errstr);
        }
        return;
    }
    printf("argv[%s]: %s\n", (char*)privdata, reply->str);

    /* Disconnect after receiving the reply to GET */
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

/*
 * This example requires Redis 7.0 or above.
 *
 * 1- Compile this file as a shared library. Directory of "redismodule.h" must
 *    be in the include path.
 *       gcc -fPIC -shared -I../../redis/src/ -I.. example-redismoduleapi.c -o example-redismoduleapi.so
 *
 * 2- Load module:
 *       redis-server --loadmodule ./example-redismoduleapi.so value
 */
int RedisModule_OnLoad(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {

    int ret = RedisModule_Init(ctx, "example-redismoduleapi", 1, REDISMODULE_APIVER_1);
    if (ret != REDISMODULE_OK) {
        printf("error module init \n");
        return REDISMODULE_ERR;
    }

    if (!RedisModule_GetServerVersion ||
        RedisModule_GetServerVersion() < 0x00070000) {
        printf("Redis 7.0 or above is required! \n");
        return REDISMODULE_ERR;
    }

    redisAsyncContext *c = redisAsyncConnect("127.0.0.1", 6379);
    if (c->err) {
        /* Let *c leak for now... */
        printf("Error: %s\n", c->errstr);
        return 1;
    }

    size_t len;
    const char *val = RedisModule_StringPtrLen(argv[argc-1], &len);

    redisModuleAttach(c);
    redisAsyncSetConnectCallback(c,connectCallback);
    redisAsyncSetDisconnectCallback(c,disconnectCallback);
    redisAsyncCommand(c, NULL, NULL, "SET key %b", val, len);
    redisAsyncCommand(c, getCallback, (char*)"end-1", "GET key");
    return 0;
}
