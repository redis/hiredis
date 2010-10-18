#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <assert.h>
#include <unistd.h>

#include "hiredis.h"

/* The following lines make up our testing "framework" :) */
static int tests = 0, fails = 0;
#define test(_s) { printf("#%02d ", ++tests); printf(_s); }
#define test_cond(_c) if(_c) printf("PASSED\n"); else {printf("FAILED\n"); fails++;}

static long long usec(void) {
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return (((long long)tv.tv_sec)*1000000)+tv.tv_usec;
}

static redisContext *blocking_context = NULL;
static void __connect(redisContext **target) {
    *target = blocking_context = redisConnect((char*)"127.0.0.1", 6379, NULL);
    if (blocking_context->error != NULL) {
        printf("Connection error: %s\n", blocking_context->error);
        exit(1);
    }
}

static void test_blocking_connection() {
    redisContext *c;
    redisReply *reply;

    __connect(&c);
    test("Returns I/O error when the connection is lost: ");
    reply = redisCommand(c,"QUIT");
    test_cond(redisCommand(c,"PING") == NULL &&
        strcasecmp(reply->reply,"OK") == 0 &&
        strcmp(c->error,"read: Server closed the connection") == 0);
    freeReplyObject(reply);
    redisFree(c);

    __connect(&c); /* reconnect */
    test("Is able to deliver commands: ");
    reply = redisCommand(c,"PING");
    test_cond(reply->type == REDIS_REPLY_STRING &&
        strcasecmp(reply->reply,"pong") == 0)
    freeReplyObject(reply);

    /* Switch to DB 9 for testing, now that we know we can chat. */
    reply = redisCommand(c,"SELECT 9");
    freeReplyObject(reply);

    /* Make sure the DB is emtpy */
    reply = redisCommand(c,"DBSIZE");
    if (reply->type != REDIS_REPLY_INTEGER ||
        reply->integer != 0) {
        printf("Sorry DB 9 is not empty, test can not continue\n");
        exit(1);
    } else {
        printf("DB 9 is empty... test can continue\n");
    }
    freeReplyObject(reply);

    test("Is a able to send commands verbatim: ");
    reply = redisCommand(c,"SET foo bar");
    test_cond (reply->type == REDIS_REPLY_STRING &&
        strcasecmp(reply->reply,"ok") == 0)
    freeReplyObject(reply);

    test("%%s String interpolation works: ");
    reply = redisCommand(c,"SET %s %s","foo","hello world");
    freeReplyObject(reply);
    reply = redisCommand(c,"GET foo");
    test_cond(reply->type == REDIS_REPLY_STRING &&
        strcmp(reply->reply,"hello world") == 0);
    freeReplyObject(reply);

    test("%%b String interpolation works: ");
    reply = redisCommand(c,"SET %b %b","foo",3,"hello\x00world",11);
    freeReplyObject(reply);
    reply = redisCommand(c,"GET foo");
    test_cond(reply->type == REDIS_REPLY_STRING &&
        memcmp(reply->reply,"hello\x00world",11) == 0)

    test("Binary reply length is correct: ");
    test_cond(sdslen(reply->reply) == 11)
    freeReplyObject(reply);

    test("Can parse nil replies: ");
    reply = redisCommand(c,"GET nokey");
    test_cond(reply->type == REDIS_REPLY_NIL)
    freeReplyObject(reply);

    /* test 7 */
    test("Can parse integer replies: ");
    reply = redisCommand(c,"INCR mycounter");
    test_cond(reply->type == REDIS_REPLY_INTEGER && reply->integer == 1)
    freeReplyObject(reply);

    test("Can parse multi bulk replies: ");
    freeReplyObject(redisCommand(c,"LPUSH mylist foo"));
    freeReplyObject(redisCommand(c,"LPUSH mylist bar"));
    reply = redisCommand(c,"LRANGE mylist 0 -1");
    test_cond(reply->type == REDIS_REPLY_ARRAY &&
              reply->elements == 2 &&
              !memcmp(reply->element[0]->reply,"bar",3) &&
              !memcmp(reply->element[1]->reply,"foo",3))
    freeReplyObject(reply);

    /* m/e with multi bulk reply *before* other reply.
     * specifically test ordering of reply items to parse. */
    test("Can handle nested multi bulk replies: ");
    freeReplyObject(redisCommand(c,"MULTI"));
    freeReplyObject(redisCommand(c,"LRANGE mylist 0 -1"));
    freeReplyObject(redisCommand(c,"PING"));
    reply = (redisCommand(c,"EXEC"));
    test_cond(reply->type == REDIS_REPLY_ARRAY &&
              reply->elements == 2 &&
              reply->element[0]->type == REDIS_REPLY_ARRAY &&
              reply->element[0]->elements == 2 &&
              !memcmp(reply->element[0]->element[0]->reply,"bar",3) &&
              !memcmp(reply->element[0]->element[1]->reply,"foo",3) &&
              reply->element[1]->type == REDIS_REPLY_STRING &&
              strcasecmp(reply->element[1]->reply,"pong") == 0);
    freeReplyObject(reply);
}

static void test_reply_reader() {
    void *reader;
    char *err;
    int ret;

    test("Error handling in reply parser: ");
    reader = redisReplyReaderCreate(NULL);
    redisReplyReaderFeed(reader,(char*)"@foo\r\n",6);
    ret = redisReplyReaderGetReply(reader,NULL);
    err = redisReplyReaderGetError(reader);
    test_cond(ret == REDIS_ERR &&
              strcasecmp(err,"protocol error, got \"@\" as reply type byte") == 0);
    redisReplyReaderFree(reader);

    /* when the reply already contains multiple items, they must be free'd
     * on an error. valgrind will bark when this doesn't happen. */
    test("Memory cleanup in reply parser: ");
    reader = redisReplyReaderCreate(NULL);
    redisReplyReaderFeed(reader,(char*)"*2\r\n",4);
    redisReplyReaderFeed(reader,(char*)"$5\r\nhello\r\n",11);
    redisReplyReaderFeed(reader,(char*)"@foo\r\n",6);
    ret = redisReplyReaderGetReply(reader,NULL);
    err = redisReplyReaderGetError(reader);
    test_cond(ret == REDIS_ERR &&
              strcasecmp(err,"protocol error, got \"@\" as reply type byte") == 0);
    redisReplyReaderFree(reader);
}

static void test_throughput() {
    int i;
    long long t1, t2;
    redisContext *c = blocking_context;
    redisReply **replies;

    test("Throughput:\n");
    for (i = 0; i < 500; i++)
        freeReplyObject(redisCommand(c,"LPUSH mylist foo"));

    replies = malloc(sizeof(redisReply*)*1000);
    t1 = usec();
    for (i = 0; i < 1000; i++) replies[i] = redisCommand(c,"PING");
    t2 = usec();
    for (i = 0; i < 1000; i++) freeReplyObject(replies[i]);
    free(replies);
    printf("\t(1000x PING: %.2fs)\n", (t2-t1)/1000000.0);

    replies = malloc(sizeof(redisReply*)*1000);
    t1 = usec();
    for (i = 0; i < 1000; i++) replies[i] = redisCommand(c,"LRANGE mylist 0 499");
    t2 = usec();
    for (i = 0; i < 1000; i++) freeReplyObject(replies[i]);
    free(replies);
    printf("\t(1000x LRANGE with 500 elements: %.2fs)\n", (t2-t1)/1000000.0);
}

static void cleanup() {
    redisContext *c = blocking_context;
    redisReply *reply;

    /* Make sure we're on DB 9 */
    reply = redisCommand(c,"SELECT 9");
    assert(reply != NULL); freeReplyObject(reply);
    reply = redisCommand(c,"FLUSHDB");
    assert(reply != NULL); freeReplyObject(reply);
    redisFree(c);
}

static long __test_callback_flags = 0;
static void __test_callback(redisContext *c, const void *privdata) {
    ((void)c);
    /* Shift to detect execution order */
    __test_callback_flags <<= 8;
    __test_callback_flags |= (long)privdata;
}

static void test_nonblocking_connection() {
    redisContext *c;
    int wdone = 0;

    __test_callback_flags = 0;
    test("Calls command callback when command is issued: ");
    c = redisConnectNonBlock("127.0.0.1", 6379, NULL);
    redisSetCommandCallback(c,__test_callback,(const void*)1);
    redisCommand(c,"PING");
    test_cond(__test_callback_flags == 1);
    redisFree(c);

    __test_callback_flags = 0;
    test("Calls disconnect callback on redisDisconnect: ");
    c = redisConnectNonBlock("127.0.0.1", 6379, NULL);
    redisSetDisconnectCallback(c,__test_callback,(const void*)2);
    redisDisconnect(c);
    test_cond(__test_callback_flags == 2);
    redisFree(c);

    __test_callback_flags = 0;
    test("Calls disconnect callback and free callback on redisFree: ");
    c = redisConnectNonBlock("127.0.0.1", 6379, NULL);
    redisSetDisconnectCallback(c,__test_callback,(const void*)2);
    redisSetFreeCallback(c,__test_callback,(const void*)4);
    redisFree(c);
    test_cond(__test_callback_flags == ((2 << 8) | 4));

    test("redisBufferWrite against empty write buffer: ");
    c = redisConnectNonBlock("127.0.0.1", 6379, NULL);
    test_cond(redisBufferWrite(c,&wdone) == REDIS_OK && wdone == 1);
    redisFree(c);

    test("redisBufferWrite against not yet connected fd: ");
    c = redisConnectNonBlock("127.0.0.1", 6379, NULL);
    redisCommand(c,"PING");
    test_cond(redisBufferWrite(c,NULL) == REDIS_ERR &&
              strncmp(c->error,"write:",6) == 0);
    redisFree(c);

    test("redisBufferWrite against closed fd: ");
    c = redisConnectNonBlock("127.0.0.1", 6379, NULL);
    redisCommand(c,"PING");
    redisDisconnect(c);
    test_cond(redisBufferWrite(c,NULL) == REDIS_ERR &&
              strncmp(c->error,"write:",6) == 0);
    redisFree(c);
}

int main(void) {
    test_blocking_connection();
    test_reply_reader();
    test_nonblocking_connection();
    test_throughput();
    cleanup();

    if (fails == 0) {
        printf("ALL TESTS PASSED\n");
    } else {
        printf("*** %d TESTS FAILED ***\n", fails);
    }
    return 0;
}
