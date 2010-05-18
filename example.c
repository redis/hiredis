#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hiredis.h"

int main(void) {
    int fd;
    redisReply *reply;

    reply = redisConnect(&fd, "127.0.0.1", 6379);
    if (reply != NULL) {
        printf("Connection error: %s", reply->reply);
        exit(1);
    }

    /* PING server */
    reply = redisCommand(fd,"PING");
    printf("PONG: %s\n", reply->reply);
    freeReplyObject(reply);

    /* Set a key */
    reply = redisCommand(fd,"SET %s %s", "foo", "hello world");
    printf("SET: %s\n", reply->reply);
    freeReplyObject(reply);

    /* Set a key using binary safe API */
    reply = redisCommand(fd,"SET %b %b", "bar", 3, "hello", 5);
    printf("SET (binary API): %s\n", reply->reply);
    freeReplyObject(reply);

    return 0;
}
