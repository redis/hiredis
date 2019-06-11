#include <functional>
#include <cstdarg>
#include <gtest/gtest.h>

#define HIREDIS_SSL

#include "hiredis.h"
#include "adapters/libevent.h"
#include "common.h"

using namespace hiredis;


struct AsyncClient;
typedef std::function<void(AsyncClient*, bool)> ConnectionCallback;
typedef std::function<void(AsyncClient*, bool)> DisconnectCallback;
typedef std::function<void(AsyncClient*, redisReply *)> CommandCallback;

static void realConnectCb(const redisAsyncContext*, int);
static void realDisconnectCb(const redisAsyncContext*, int);
static void realCommandCb(redisAsyncContext*, void*, void*);

struct CmdData {
    AsyncClient *client;
    CommandCallback cb;
};

struct privData {
    AsyncClient *c;
    redisReply *r;
};

struct AsyncClient {
    void connectCommon(const redisOptions& orig, event_base *b, unsigned timeoutMs) {
        redisOptions options = orig;
        struct timeval tv = { 0 };
        if (timeoutMs) {
            tv.tv_usec = timeoutMs * 1000;
            options.timeout = &tv;
        }

        ac = redisAsyncConnectWithOptions(&options);
        ac->data = this;
        redisLibeventAttach(ac, b);
        redisAsyncSetConnectCallback(ac, realConnectCb);
        redisAsyncSetDisconnectCallback(ac, realDisconnectCb);
    }

    AsyncClient(const redisOptions& options, event_base* b, unsigned timeoutMs = 0) {
        connectCommon(options, b, timeoutMs);
    }

    AsyncClient(const ClientSettings& settings, event_base *b, unsigned timeoutMs = 0) {
        redisOptions options = { 0 };
        settings.initOptions(options);
        connectCommon(options, b, timeoutMs);

        if (ac->c.err != REDIS_OK) {
            ClientError::throwContext(&ac->c);
        }

        if (settings.is_ssl()) {
            printf("Securing async connection...\n");
            int rc = redisSecureConnection(&ac->c,
                    settings.ssl_ca(), settings.ssl_cert(), settings.ssl_key(),
                    NULL);
            if (rc != REDIS_OK) {
                throw SSLError(ac->c.errstr);
            }
        }
    }

    AsyncClient(redisAsyncContext *ac, event_base *b) : ac(ac) {
        ac->data = this;
        redisLibeventAttach(ac, b);
        redisAsyncSetConnectCallback(ac, realConnectCb);
        redisAsyncSetDisconnectCallback(ac, realDisconnectCb);
    }

    void onConnect(ConnectionCallback cb) {
        conncb = cb;
    }

    ~AsyncClient() {
        if (ac != NULL) {
            redisAsyncContext *tmpac = ac;
            ac = NULL;
            redisAsyncDisconnect(tmpac);
        }
    }
    
    template<typename... Args>
    void cmd(CommandCallback cb, const char *fmt, Args ... args) {
        auto data = new CmdData {this, cb };
        redisAsyncCommand(ac, realCommandCb, data, fmt, args...); //for coverage purposes
    }

    void cmdFormatted(CommandCallback cb, const char *cmd, size_t len) {
        auto data = new CmdData {this, cb };
        redisAsyncFormattedCommand(ac, realCommandCb, data, cmd, len); //for coverage purposes
    }

    void disconnect(DisconnectCallback cb) {
        disconncb = cb;
        auto tmpac = ac;
        ac = NULL;
        redisAsyncDisconnect(tmpac);
    }

    void disconnect() {
        if(ac != NULL) {
            auto tmpac = ac;
            ac = NULL;
            redisAsyncDisconnect(tmpac);
        }
    }

    ConnectionCallback conncb;
    DisconnectCallback disconncb;
    redisAsyncContext *ac;
};

static void realConnectCb(const redisAsyncContext *ac, int status) {
    auto self = reinterpret_cast<AsyncClient*>(ac->data);
    if (self->conncb) {
        self->conncb(self, status == 0);
    }
}

static void realDisconnectCb(const redisAsyncContext *ac, int status) {
    auto self = reinterpret_cast<AsyncClient*>(ac->data);
    if (self->disconncb) {
        self->disconncb(self, status == 0);
    }
}

static void realCommandCb(redisAsyncContext *ac, void *r, void *ctx) {
    auto *d = reinterpret_cast<CmdData*>(ctx);
    auto *rep = reinterpret_cast<redisReply*>(r);
    auto *self = reinterpret_cast<AsyncClient*>(ac->data);
    d->cb(self, rep);
    delete d;
}

void cmdCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = (redisReply*)r;
    AsyncClient *client = reinterpret_cast<AsyncClient*>(privdata);
    ASSERT_TRUE(c);
    ASSERT_EQ(reply->type, REDIS_REPLY_STATUS);
    ASSERT_STREQ("OK", reply->str);
    client->disconnect();
}

class AsyncTest : public ::testing::Test {
protected:
    void SetUp() override {
        libevent = event_base_new();
    }
    void TearDown() override {
        event_base_free(libevent);
        libevent = NULL;
    }
    void wait() {
        event_base_dispatch(libevent);
    }
    event_base *libevent;
};

TEST_F(AsyncTest, testAsync) {
    AsyncClient client(settings_g, libevent, 1000);

    bool gotConnect = false;
    bool gotCommand = false;
    client.onConnect([&](AsyncClient*, bool status) {
        ASSERT_TRUE(status);
        gotConnect = true;
    });

    client.cmd([&](AsyncClient *c, redisReply *r) {
        ASSERT_TRUE(r != NULL);
        ASSERT_EQ(REDIS_REPLY_STATUS, r->type);
        ASSERT_STREQ("PONG", r->str);
        c->disconnect();
        gotCommand = true;
    }, "PING");
    wait();
    ASSERT_TRUE(gotConnect);
    ASSERT_TRUE(gotCommand);
}

TEST_F(AsyncTest, testCmd) {
    AsyncClient client(settings_g, libevent, 1000);

    client.cmd([&](AsyncClient *c, redisReply *r) {
        ASSERT_TRUE(r != NULL);
        ASSERT_EQ(REDIS_REPLY_STATUS, r->type);
        ASSERT_STREQ("OK", r->str);
    }, "SET foo bar");

    client.cmd([&](AsyncClient *c, redisReply *r) {
        ASSERT_TRUE(r != NULL);
        ASSERT_EQ(REDIS_REPLY_STATUS, r->type);
        ASSERT_STREQ("OK", r->str);
        c->disconnect();
    }, "SET %s %s", "foo", "hello world");
    wait();
}

TEST_F(AsyncTest, testGetFirst) {
    AsyncClient client(settings_g, libevent, 1000);
    client.cmd([&](AsyncClient *c, redisReply *r) {
        ASSERT_TRUE(r != NULL);
        ASSERT_EQ(REDIS_REPLY_NIL, r->type);
        ASSERT_STREQ(NULL, r->str);
        c->disconnect();
    }, "GET xyzzy");
    wait();
}

TEST_F(AsyncTest, testWrongIP) {
    redisAsyncContext *ac = redisAsyncConnect("localhost", 6378);
    AsyncClient client(ac, libevent);
    client.cmd([&](AsyncClient *c, redisReply *r) {
        ASSERT_TRUE(r == NULL);
        ASSERT_EQ(c->ac->err, REDIS_ERR_IO);
        ASSERT_STRCASEEQ(c->ac->errstr, "Connection refused");
    }, "PING");
    wait();
}

TEST_F(AsyncTest, testError) {
    AsyncClient client(settings_g, libevent, 1000);
    client.cmd([&](AsyncClient *c, redisReply *r) {
        ASSERT_TRUE(r != NULL);
        ASSERT_EQ(REDIS_REPLY_ERROR, r->type);
        ASSERT_STREQ( r->str, 
            "ERR unknown command `PONG`, with args beginning with: ");
        c->disconnect();
    }, "PONG");
    wait();
}

void subUnsubFunc(redisAsyncContext *c, void *reply, void *privdata) {
    redisReply *r = (redisReply *)reply;
    event_base *base = (event_base *)privdata;
    ASSERT_EQ(c->err, 0);
    if (reply == NULL) return;
    if (r->type == REDIS_REPLY_ARRAY) { 
        if(r->elements == 2) ASSERT_STRCASEEQ("pong", r->element[0]->str);       
        else if(strcasecmp(r->element[0]->str, "unsubscribe") == 0)
        {
            event_base_loopbreak(base);
        }
    } 
}

TEST_F(AsyncTest, testSubUnsub1) {
    AsyncClient client(settings_g, libevent);

    redisAsyncCommand(client.ac, subUnsubFunc, libevent, "SUBSCRIBE foo");
    redisAsyncCommand(client.ac, subUnsubFunc, libevent, "PING");    
    redisAsyncCommand(client.ac, subUnsubFunc, libevent, "UNSUBSCRIBE foo");

    wait(); 
}

void subPongFunc(redisAsyncContext *c, void *reply, void *privdata) {
    AsyncClient *self = reinterpret_cast<AsyncClient*>(c->data);
    if(c->err != 0) {
        ASSERT_STREQ("ERR unknown command `PONG`, with args beginning with: ",
            c->errstr);
        self->disconnect();
    }
}

TEST_F(AsyncTest, testSubUnsub2) {
    AsyncClient client(settings_g, libevent);

    redisAsyncCommand(client.ac, subPongFunc, &client, "SUBSCRIBE foo");
    redisAsyncCommand(client.ac, subPongFunc, &client, "PONG");    

    wait(); 
}

void monitorFunc(redisAsyncContext *c, void *reply, void *privdata) {
    redisReply *r = (redisReply *)reply;
    event_base *base = (event_base *)privdata;
    if (reply == NULL) return;
    ASSERT_TRUE(r->type == 5);
    if(strcasecmp(r->str, "OK") == 0) return;
    if(r->len == 4) ASSERT_STRCASEEQ(r->str, "pong");
    else if (r->len == 44) {
        ASSERT_TRUE(strncasecmp("ping", r->str + 39, 4) == 0);       
    } else if (r->len == 55) {
        ASSERT_TRUE(strncasecmp("set", r->str + 39, 3) == 0);       
        event_base_loopbreak(base); 
    }
}

TEST_F(AsyncTest, testMonitor) {
    AsyncClient client(settings_g, libevent);

    redisAsyncCommand(client.ac, monitorFunc, libevent, "MONITOR");
    redisAsyncCommand(client.ac, monitorFunc, libevent, "PING");    
    redisAsyncCommand(client.ac, monitorFunc, libevent, "SET foo bar");    

    wait(); 
}

TEST_F(AsyncTest, testConTimeout) {
    AsyncClient client(settings_g, libevent, 100);
    client.cmd([&](AsyncClient *c, redisReply *r) { }, "DEBUG SLEEP 1");
    client.cmd([&](AsyncClient *c, redisReply *r) { 
        ASSERT_TRUE(r == NULL);
        ASSERT_EQ(REDIS_REPLY_ERROR, r->type);
        ASSERT_STREQ(NULL, r->str);
        ASSERT_EQ(c->ac->err, REDIS_ERR_TIMEOUT);
        redisAsyncHandleTimeout(client.ac);
    }, "PING");
}

TEST_F(AsyncTest, testSetTimeout) {
    redisAsyncContext *ac = redisAsyncConnect("localhost", 6379);
    ASSERT_TRUE(ac->c.timeout == NULL);
    struct timeval tv = { 0, 1000 };
    redisAsyncSetTimeout(ac,tv);
    ASSERT_TRUE(ac->c.timeout->tv_usec == 1000);
    tv = { 0, 1000 };
    redisAsyncSetTimeout(ac,tv);
    ASSERT_TRUE(ac->c.timeout->tv_usec == 1000);


    AsyncClient client(ac, libevent);
    redisAsyncCommand(client.ac, cmdCallback, &client, "SET foo bar"); 
}

class ClientTestAsyncConnections : public ::testing::Test {
};

TEST_F(ClientTestAsyncConnections, testAsyncBlocking) {
    redisAsyncContext *actx = NULL;
    actx = redisAsyncConnectBind("localhost", 6379, "8.8.8.8");
    ASSERT_TRUE(actx);
    redisAsyncFree(actx);

    actx = redisAsyncConnectBindWithReuse("localhost", 6379, "8.8.8.8");
    ASSERT_TRUE(actx);
    redisAsyncFree(actx);

    actx = redisAsyncConnectUnix("8.8.8.8");
    ASSERT_TRUE(actx);
    redisAsyncFree(actx);
}

TEST_F(ClientTestAsyncConnections, testAsyncArgv) {
    redisAsyncContext *actx = redisAsyncConnect("localhost", 6379);
    char *argv[3];
    size_t argvlen[3];
    int argc = 3;

    argv[0] = (char *)"HMSET";
    argvlen[0] = 5;
    argv[1] = (char *)"key";
    argvlen[1] = 3;
    argv[2] = (char *)"x";
    argvlen[2] = 1;

    ASSERT_TRUE(redisAsyncCommandArgv(actx, NULL, NULL, argc, (const char **)argv, argvlen) == REDIS_OK);
}

TEST_F(ClientTestAsyncConnections, testAsyncFormattedCmd) {
    redisAsyncContext *actx = redisAsyncConnect("localhost", 6379);
    char *cmd;
    int len;

    len = redisFormatCommand(&cmd, "SET foo bar");
    ASSERT_TRUE(redisAsyncFormattedCommand(actx, NULL, NULL, cmd, len) == REDIS_OK);
}

/*
TEST_F(ClientTestAsyncConnections, testAsyncSSL) {
    redisAsyncContext *actx = redisAsyncConnect("10.0.0.112", 16379);
   // actx->c.flags |= REDIS_SSL;
    void *reply;
    char *cmd;
    int len;

    len = redisFormatCommand(&cmd, "SET foo bar");
    ASSERT_TRUE(redisAsyncFormattedCommand(actx, NULL, NULL, cmd, len) == REDIS_OK);

    redisAsyncCommand(actx, cmdCallback, NULL, "SET foo bar"); 
    redisAsyncHandleWrite(actx);
    redisAsyncHandleRead(actx);

    redisGetReply(&actx->c, &reply);


}*/