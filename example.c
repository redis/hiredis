#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hiredis.h"

int main(void) {
    int fd;
    unsigned int j;
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

    /* Try a GET and two INCR */
    reply = redisCommand(fd,"GET foo");
    printf("GET foo: %s\n", reply->reply);
    freeReplyObject(reply);

    reply = redisCommand(fd,"INCR counter");
    printf("INCR counter: %lld\n", reply->integer);
    freeReplyObject(reply);
    /* again ... */
    reply = redisCommand(fd,"INCR counter");
    printf("INCR counter: %lld\n", reply->integer);
    freeReplyObject(reply);

    /* Create a list of numbers, from 0 to 9 */
    reply = redisCommand(fd,"DEL mylist");
    freeReplyObject(reply);
    for (j = 0; j < 10; j++) {
        char buf[64];

        snprintf(buf,64,"%d",j);
        reply = redisCommand(fd,"LPUSH mylist element-%s", buf);
        freeReplyObject(reply);
    }

    /* Let's check what we have inside the list */
    reply = redisCommand(fd,"LRANGE mylist 0 -1");
    if (reply->type == REDIS_REPLY_ARRAY) {
        for (j = 0; j < reply->elements; j++) {
            printf("%u) %s\n", j, reply->element[j]->reply);
        }
    }
    freeReplyObject(reply);

    return 0;
}
