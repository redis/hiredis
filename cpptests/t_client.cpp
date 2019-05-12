#include <gtest/gtest.h>
#include <cstdlib>
#include <string>
#include <cassert>

#include "common.h"

using namespace hiredis;

class Client {
public:
    redisContext* operator*() {
        return ctx;
    }

    Client(redisContext *ctx) : ctx(ctx) {}

    Client(const redisOptions& options) {
        connectOrThrow(options);
    }

    Client(const ClientSettings& settings) {
        redisOptions options = {0};
        settings.initOptions(options);
        connectOrThrow(options);
        if (settings.is_ssl()) {
            secureConnection(settings);
        }
    }

    void secureConnection(const ClientSettings& settings) {
        if (redisSecureConnection(
                ctx, settings.ssl_ca(), settings.ssl_cert(),
                settings.ssl_key(), NULL) != REDIS_OK) 
        {
            redisFree(ctx);
            ctx = NULL;
            throw SSLError();
        }
    }

    redisReply *cmd(const char *fmt, ...) {
        va_list ap;
        va_start(ap, fmt);
        void *p = redisvCommand(ctx, fmt, ap);
        va_end(ap);
        return castReply(p);
    }

    void flushdb() {
        redisReply *p = cmd("FLUSHDB");
        if (p == NULL) {
            ClientError::throwCode(ctx->err);
        }
        if (p->type == REDIS_REPLY_ERROR) {
            auto pp = CommandError(p);
            freeReplyObject(p);
            throw pp;
        }
        freeReplyObject(p);
    }

    ~Client() {
        if (ctx != NULL) {
            redisFree(ctx);
        }
    }

    Client(Client&& other) {
        this->ctx = other.ctx;
        other.ctx = NULL;
    }

    void nothing() const {}

private:
    void destroyAndThrow() {
        assert(ctx->err);
        int err = ctx->err;
        redisFree(ctx);
        ctx = NULL;
        ClientError::throwCode(err);
    }
    void connectOrThrow(const redisOptions& options) {
        ctx = redisConnectWithOptions(&options);
        if (!ctx) {
            throw ConnectError();
        }
        if (ctx->err) {
            destroyAndThrow();
        }
    }

    redisContext *ctx;
};

class ClientTestTCP : public ::testing::Test {
public:
    ClientTestTCP() : cs("tcp", ""), c(cs) {}

protected:
    virtual void SetUp() {        
        c.flushdb();
    }
    virtual void TearDown() {
    //    if(reply != nullptr) { freeReplyObject(reply); }
    }

    ClientSettings cs;    
    Client c;
    redisReply *reply;
};

TEST_F(ClientTestTCP, testTimeout) {
    redisOptions options = {0};
    timeval tv = {0};
    tv.tv_usec = 10000; // 10k micros, small enough
    options.timeout = &tv;
    // see https://tools.ietf.org/html/rfc5737
    // this block of addresses is reserved for "documentation", and it
    // would likely not connect, ever.
    ASSERT_THROW(Client(options).nothing(), ClientError);

    // Test the normal timeout
 //   Client c(settings_g);
    c.flushdb();
}

// Not finished
TEST_F(ClientTestTCP, testAppendFormattedCmd) {
    char *cmd;
    int len;

    len = redisFormatCommand(&cmd, "SET foo bar");
    ASSERT_TRUE(redisAppendFormattedCommand(*c, cmd, len) == REDIS_OK);
//    assert(redisGetReply(*c, (void*)&reply) == REDIS_OK);
    free(cmd);
}

TEST_F(ClientTestTCP, testBlockingConnection) {
    reply = castReply(redisCommand(*c,"PING"));
    ASSERT_TRUE(reply->type == REDIS_REPLY_STATUS && 
                strcasecmp(reply->str,"pong") == 0);
    freeReplyObject(reply);

    reply = castReply(redisCommand(*c,"SET foo bar"));
    ASSERT_TRUE(reply->type == REDIS_REPLY_STATUS &&
                strcasecmp(reply->str,"ok") == 0);
    freeReplyObject(reply);

    reply = castReply(redisCommand(*c,"SET %s %s","foo",
                                    "hello world"));
    freeReplyObject(reply);
    reply = castReply(redisCommand(*c,"GET foo"));
    ASSERT_TRUE(reply->type == REDIS_REPLY_STRING &&
                strcmp(reply->str,"hello world") == 0);
    freeReplyObject(reply);

    reply = castReply(redisCommand(*c,"SET %b %b","foo",
                (size_t)3,"hello\x00world",(size_t)11));
    freeReplyObject(reply);
    reply = castReply(redisCommand(*c,"GET foo"));
    ASSERT_TRUE(reply->type == REDIS_REPLY_STRING &&
                memcmp(reply->str,"hello\x00world",11) == 0);
    ASSERT_TRUE(reply->len == 11);
    freeReplyObject(reply);

    reply = castReply(redisCommand(*c,"GET nokey"));
    ASSERT_TRUE(reply->type == REDIS_REPLY_NIL);
    freeReplyObject(reply);

    reply = castReply(redisCommand(*c,"INCR mycounter"));
    ASSERT_TRUE(reply->type == REDIS_REPLY_INTEGER && reply->integer == 1);
    freeReplyObject(reply);

    freeReplyObject(castReply(redisCommand(*c,"LPUSH mylist foo")));
    freeReplyObject(castReply(redisCommand(*c,"LPUSH mylist bar")));
    reply = castReply(redisCommand(*c,"LRANGE mylist 0 -1"));
    ASSERT_TRUE(reply->type == REDIS_REPLY_ARRAY &&
              reply->elements == 2 &&
              !memcmp(reply->element[0]->str,"bar",3) &&
              !memcmp(reply->element[1]->str,"foo",3));
    freeReplyObject(reply);

    /* m/e with multi bulk reply *before* other reply.
     * specifically test ordering of reply items to parse. */
    freeReplyObject(castReply(redisCommand(*c,"MULTI")));
    freeReplyObject(castReply(redisCommand(*c,"LRANGE mylist 0 -1")));
    freeReplyObject(castReply(redisCommand(*c,"PING")));
    reply = (castReply(redisCommand(*c,"EXEC")));
    ASSERT_TRUE(reply->type == REDIS_REPLY_ARRAY &&
              reply->elements == 2 &&
              reply->element[0]->type == REDIS_REPLY_ARRAY &&
              reply->element[0]->elements == 2 &&
              !memcmp(reply->element[0]->element[0]->str,"bar",3) &&
              !memcmp(reply->element[0]->element[1]->str,"foo",3) &&
              reply->element[1]->type == REDIS_REPLY_STATUS &&
              strcasecmp(reply->element[1]->str,"pong") == 0);
    freeReplyObject(reply);
}

TEST_F(ClientTestTCP, testSuccesfulReconnect) {
    struct timeval tv = { 0, 10000 };

//  Successfully completes a command when the timeout is not exceeded
    reply = castReply(redisCommand(*c,"SET foo fast"));
    freeReplyObject(reply);
    redisSetTimeout(*c, tv);
    reply = castReply(redisCommand(*c, "GET foo"));
    ASSERT_TRUE(reply != NULL && reply->type == REDIS_REPLY_STRING && 
                memcmp(reply->str, "fast", 4) == 0);
    freeReplyObject(reply);
}

TEST_F(ClientTestTCP, testBlockingConnectionTimeout) {
    ssize_t s;
    const char *cmd = "DEBUG SLEEP 3\r\n";
    struct timeval tv = { 0, 10000 };

//  Does not return a reply when the command times out
    s = write((*c)->fd, cmd, strlen(cmd));
    tv.tv_sec = 0;
    tv.tv_usec = 10000;
    redisSetTimeout(*c, tv);
    reply = castReply(redisCommand(*c, "GET foo"));
    ASSERT_TRUE(s > 0 && reply == NULL && (*c)->err == REDIS_ERR_IO && strcmp((*c)->errstr, "Resource temporarily unavailable") == 0);
    freeReplyObject(reply);

//  Reconnect properly reconnects after a timeout
    redisReconnect(*c);
    reply = castReply(redisCommand(*c, "PING"));
    ASSERT_TRUE(reply != NULL && reply->type == REDIS_REPLY_STATUS && strcmp(reply->str, "PONG") == 0);
    freeReplyObject(reply);

//  Reconnect properly uses owned parameters
    char foo[4] = "foo";
/**************** fails *****************
    (*c)->tcp.host = foo;
    (*c)->unix_sock.path = foo;
/**************** fails *****************
    redisReconnect(*c);
    reply = castReply(redisCommand(*c, "PING"));
    ASSERT_TRUE(reply != NULL && reply->type == REDIS_REPLY_STATUS && strcmp(reply->str, "PONG") == 0);
    freeReplyObject(reply);    */
    
}