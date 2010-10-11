#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <hiredis/libevent.h>
#include <signal.h>

void getCallback(redisContext *c, redisReply *reply, const void *privdata) {
    printf("argv[%s]: %s\n", (const char*)privdata, reply->reply);

    /* Disconnect after receiving the reply to GET */
    redisDisconnect(c);
}

void errorCallback(redisContext *c) {
    printf("Error: %s\n", c->error);

    /* Clean up the context when there was an error */
    redisFree(c);
}

int main (int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
    struct event_base *base = event_base_new();

    redisContext *c = redisLibEventConnect("127.0.0.1", 6379, errorCallback, base);
    if (c == NULL) return 1;

    redisCommand(c, "SET key %b", argv[argc-1], strlen(argv[argc-1]));
    redisCommandWithCallback(c, getCallback, "end-1", "GET key");
    event_base_dispatch(base);
    redisFree(c);
    return 0;
}
