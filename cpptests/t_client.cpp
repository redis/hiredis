#include <gtest/gtest.h>
#include <cstdlib>
#include <string>
#include <cassert>
#include <climits>
#include <netdb.h>

#include "hiredis.h"
#include "common.h"

namespace hiredis {

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

    Client& operator=(const Client &other_) {
        memcpy(ctx, other_.ctx, sizeof(redisContext));
        return *this;
    }

    operator redisContext*() const { return ctx; }

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

    RedisReply cmd(const char *fmt, ...) {
        va_list ap;
        va_start(ap, fmt);
        RedisReply p = redisvCommand(ctx, fmt, ap);
        va_end(ap);
        return p;
    }

    void flushdb() {
        RedisReply p = cmd("FLUSHDB");
        if (p == NULL) {
            ClientError::throwCode(ctx->err);
        }
        if (p->type == REDIS_REPLY_ERROR) {
            auto pp = CommandError(p);
        //    freeReplyObject(p);
            throw pp;
        }
      //  freeReplyObject(p);
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
    ClientTestTCP() : c(cs), reply(nullptr) {}

protected:
    virtual void SetUp() {        
        c.flushdb();
    }
    virtual void TearDown() {
        reply.Destroy();
    }

    ClientSettings cs;    
    Client c;
    RedisReply reply;
};

TEST_F(ClientTestTCP, testAppendFormattedCmd) {
    char *cmd;
    int len;

    len = redisFormatCommand(&cmd, "SET foo bar");
    ASSERT_TRUE(redisAppendFormattedCommand(c, cmd, len) == REDIS_OK);
    assert(redisGetReply(c, (void**)&reply) == REDIS_OK);
    free(cmd);
}

TEST_F(ClientTestTCP, testBlockingConnection) {
    reply = redisCommand(c,"PING");
    ASSERT_TRUE(reply->type == REDIS_REPLY_STATUS);
    ASSERT_STRCASEEQ(reply->str,"pong");
    reply.Destroy();

    reply = redisCommand(c,"SET foo bar");
    ASSERT_TRUE(reply->type == REDIS_REPLY_STATUS);
    ASSERT_STRCASEEQ(reply->str,"ok");
    reply.Destroy();

    reply = redisCommand(c,"SET %s %s","foo", "hello world");
    reply.Destroy();
    
    reply = redisCommand(c,"GET foo");
    ASSERT_TRUE(reply->type == REDIS_REPLY_STRING);
    ASSERT_STRCASEEQ(reply->str,"hello world");
    reply.Destroy();
    
    reply = redisCommand(c,"SET %b %b","foo", (size_t)3,"hello\x00world",(size_t)11);
    ASSERT_STRCASEEQ(reply->str,"OK");
    reply.Destroy();
    
    reply = redisCommand(c,"GET foo");
    ASSERT_TRUE(reply->type == REDIS_REPLY_STRING &&
                memcmp(reply->str,"hello\x00world",11) == 0);
    ASSERT_TRUE(reply->len == 11);
    reply.Destroy();
    
    reply = redisCommand(c,"GET nokey");
    ASSERT_TRUE(reply->type == REDIS_REPLY_NIL);
    reply.Destroy();
    
    reply = redisCommand(c,"INCR mycounter");
    ASSERT_TRUE(reply->type == REDIS_REPLY_INTEGER && reply->integer == 1);
    reply.Destroy();
    
    freeReplyObject(redisCommand(c,"LPUSH mylist foo"));
    freeReplyObject(redisCommand(c,"LPUSH mylist bar"));
    reply = redisCommand(c,"LRANGE mylist 0 -1");
    ASSERT_TRUE(reply->type == REDIS_REPLY_ARRAY &&
              reply->elements == 2 &&
              !memcmp(reply->element[0]->str,"bar",3) &&
              !memcmp(reply->element[1]->str,"foo",3));
    reply.Destroy();
    
    /* m/e with multi bulk reply *before* other reply.
     * specifically test ordering of reply items to parse. */
    freeReplyObject(redisCommand(c,"MULTI"));
    freeReplyObject(redisCommand(c,"LRANGE mylist 0 -1"));
    freeReplyObject(redisCommand(c,"PING"));
    reply = redisCommand(c,"EXEC");
    ASSERT_TRUE(reply->type == REDIS_REPLY_ARRAY);
    ASSERT_TRUE(reply->elements == 2);
    ASSERT_TRUE(reply->element[0]->type == REDIS_REPLY_ARRAY);
    ASSERT_TRUE(reply->element[0]->elements == 2);
    ASSERT_FALSE(memcmp(reply->element[0]->element[0]->str,"bar",3));
    ASSERT_FALSE(memcmp(reply->element[0]->element[1]->str,"foo",3));
    ASSERT_TRUE(reply->element[1]->type == REDIS_REPLY_STATUS);
    ASSERT_STRCASEEQ(reply->element[1]->str,"pong");
}

TEST_F(ClientTestTCP, testSuccesfulReconnect) {
    struct timeval tv = { 0, 10000 };

//  Successfully completes a command when the timeout is not exceeded
    reply = redisCommand(c,"SET foo fast");
    reply.Destroy();
    redisSetTimeout(c, tv);
    reply = redisCommand(c, "GET foo");
    ASSERT_TRUE(reply != NULL && reply->type == REDIS_REPLY_STRING && 
                memcmp(reply->str, "fast", 4) == 0);
}

TEST_F(ClientTestTCP, testBlockingConnectionTimeout) {
    ssize_t s;
    const char *cmd = "DEBUG SLEEP 1\r\n"; // TODO: change back to 3 in future
    struct timeval tv = { 0, 10000 };

//  Does not return a reply when the command times out
    s = write((*c)->fd, cmd, strlen(cmd));
    tv.tv_sec = 0;
    tv.tv_usec = 10000;
    redisSetTimeout(c, tv);
    reply = redisCommand(c, "GET foo");
    ASSERT_TRUE(s > 0 && reply == NULL && (*c)->err == REDIS_ERR_IO);
    ASSERT_STREQ((*c)->errstr, "Resource temporarily unavailable");
    reply.Destroy();

//  Reconnect properly reconnects after a timeout
    redisReconnect(c);
    reply = redisCommand(c, "PING");
    ASSERT_TRUE(reply != NULL && reply->type == REDIS_REPLY_STATUS);
    ASSERT_STREQ(reply->str, "PONG");

//  Reconnect properly uses owned parameters
//    char foo[4] = "foo";
/**************** fails *****************
    (*c)->tcp.host = foo;
    (*c)->unix_sock.path = foo;
**************** fails *****************
    redisReconnect(c);
    reply = redisCommand(c, "PING");
    ASSERT_TRUE(reply != NULL && reply->type == REDIS_REPLY_STATUS);
    ASSERT_STREQ(reply->str, "PONG");
    freeReplyObject(reply);    */
    
}

#define HIREDIS_BAD_DOMAIN "idontexist-noreally.com"
TEST_F(ClientTestTCP, testBlockConnectionError) {
//  Returns error when host cannot be resolved
    struct addrinfo hints = { 0 };
    hints.ai_family = AF_INET;
    struct addrinfo *ai_tmp = NULL;
    bool flag = false;

    int rv = getaddrinfo(HIREDIS_BAD_DOMAIN, "6379", &hints, &ai_tmp);
    if (rv != 0) {
    // First see if this domain name *actually* resolves to NXDOMAIN
        cs.setHost(HIREDIS_BAD_DOMAIN);
        try { c = Client(cs); }
        catch(...) { flag = true; }
        ASSERT_TRUE(flag);
        flag = false;
    } else {
        printf("Skipping NXDOMAIN test. Found evil ISP!\n");
        freeaddrinfo(ai_tmp);
    }

//  Returns error when the port is not open
    cs.setHost("localhost:1");
    try { c = Client(cs); }
    catch(...) { flag = true; }
    ASSERT_TRUE(flag);
    flag = false;
 
//  Returns error when the unix_sock socket path doesn't accept connections
    cs.setUnix("/tmp/idontexist.sock");
    try { c = Client(cs); }
    catch(...) { flag = true; }
    ASSERT_TRUE(flag);
}

TEST_F(ClientTestTCP, testBlockingIO) {
    int major, minor;
    {
        /* Find out Redis version to determine the path for the next test */
        const char *field = "redis_version:";
        char *p, *eptr;

        reply = redisCommand(c,"INFO");
        p = strstr(reply->str,field);
        major = strtol(p+strlen(field),&eptr,10);
        p = eptr+1; /* char next to the first "." */
        minor = strtol(p,&eptr,10);
        reply.Destroy();
    }
    reply = redisCommand(c,"QUIT");
    if (major > 2 || (major == 2 && minor > 0)) {
        /* > 2.0 returns OK on QUIT and read() should be issued once more
         * to know the descriptor is at EOF. */
        ASSERT_STREQ(reply->str,"OK");
        ASSERT_TRUE(redisGetReply(c, (void **)&reply) == REDIS_ERR);
        reply.Destroy();
    } else {
        ASSERT_TRUE(reply == NULL);
    }

    assert((*c)->err == REDIS_ERR_EOF &&
        strcmp((*c)->errstr,"Server closed the connection") == 0);
}

TEST_F(ClientTestTCP, testBlockingIOTimeout) {
//  Returns I/O error on socket timeout
    struct timeval tv = { 0, 1000 };
    assert(redisSetTimeout(c,tv) == REDIS_OK);
    ASSERT_TRUE(redisGetReply(c, (void **)&reply) == REDIS_ERR &&
        (*c)->err == REDIS_ERR_IO && errno == EAGAIN);
}

TEST_F(ClientTestTCP, testInvalidTimeoutErr) {
//  Set error when an invalid timeout usec value is given to redisConnectWithTimeout
    redisOptions options = { 0 };
    struct timeval timeout = { 0, 10000001 };
    bool flag = false;

    options.type = REDIS_CONN_TCP;
    options.endpoint.tcp.ip = "localhost";
    options.endpoint.tcp.port = 6379;
    options.timeout = &timeout;
    
    try { c = Client(options); }
    catch(IOError e) { flag = true; }
    ASSERT_TRUE(flag);
    flag = false;

    timeout = { (((LONG_MAX) - 999) / 1000) + 1, 0 };
    try { c = Client(options); }
    catch(IOError e) { flag = true; }
    ASSERT_TRUE(flag);
}

}