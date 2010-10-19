#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <hiredis/libev.h>
#include <signal.h>

void getCallback(redisContext *c, redisReply *reply, void *privdata) {
    if (reply == NULL) return; /* Error */
    printf("argv[%s]: %s\n", (char*)privdata, reply->reply);

    /* Disconnect after receiving the reply to GET */
    redisDisconnect(c);
}

void errorCallback(const redisContext *c) {
    printf("Error: %s\n", c->error);
}

int main (int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
    struct ev_loop *loop = ev_default_loop(0);

    redisContext *c = libevRedisConnect(loop, errorCallback, "127.0.0.1", 6379);
    if (c == NULL) return 1;

    redisCommand(c, "SET key %b", argv[argc-1], strlen(argv[argc-1]));
    redisCommandWithCallback(c, getCallback, (char*)"end-1", "GET key");
    ev_loop(loop, 0);
    redisFree(c);
    return 0;
}
