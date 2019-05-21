#include <gtest/gtest.h>
#include <hiredis.h>
#include "adapters/libevent.h"
#include <functional>
#include <cstdarg>
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
            auto tmpac = ac;
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
        auto tmpac = ac;
        ac = NULL;
        redisAsyncDisconnect(tmpac);
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

TEST_F(AsyncTest, testAsync1) {
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
        ASSERT_STREQ("OK", r->str);
        c->disconnect();
        gotCommand = true;
    }, "SET foo bar");
    wait();
    ASSERT_TRUE(gotConnect);
    ASSERT_TRUE(gotCommand);
}

TEST_F(AsyncTest, testAsync2) {
    /* possible bug?? 
    redisAsyncContext *ac = redisAsyncConnectBind("localhost", 6379, "gtestname");
    AsyncClient client(ac, libevent); */

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
        ASSERT_STREQ("OK", r->str);
        c->disconnect();
        gotCommand = true;
    }, "SET %s %s","foo", "hello world");
    wait();
    ASSERT_TRUE(gotConnect);
    ASSERT_TRUE(gotCommand);
}

TEST_F(AsyncTest, testConTypes) {
    redisAsyncContext *ac = redisAsyncConnect("localhost", 6379);
    ASSERT_TRUE(ac->c.tcp.port == 6379);
    AsyncClient client(ac, libevent);

    bool gotConnect = false;
    bool gotCommand = false;
    client.onConnect([&](AsyncClient*, bool status) {
        ASSERT_TRUE(status);
        gotConnect = true;
    });

    client.cmd([&](AsyncClient *c, redisReply *r) {
        ASSERT_TRUE(r != NULL);
        ASSERT_EQ(REDIS_REPLY_STATUS, r->type);
        ASSERT_STREQ("OK", r->str);
        c->disconnect();
        gotCommand = true;
    }, "SET %s %s","foo", "hello world");
    wait();
    ASSERT_TRUE(gotConnect);
    ASSERT_TRUE(gotCommand);
}

TEST_F(AsyncTest, testConTimeout) {
    redisAsyncContext *ac = redisAsyncConnect("localhost", 6379);
    AsyncClient client(ac, libevent);
    redisAsyncHandleTimeout(ac);
    ASSERT_EQ(ac->err, REDIS_ERR_TIMEOUT);
}

/*
TEST_F(AsyncTest, testFormattedCmd) {
    AsyncClient client(settings_g, libevent, 1000);
    
    bool gotConnect = false;
    bool gotCommand = false;
    client.onConnect([&](AsyncClient*, bool status) {
        ASSERT_TRUE(status);
        gotConnect = true;
    });

    const char *str = "$ SET foo bar\r";
    client.cmdFormatted([&](AsyncClient *c, redisReply *r) {
        ASSERT_TRUE(r != NULL);
        ASSERT_EQ(REDIS_REPLY_STATUS, r->type);
        ASSERT_STREQ("OK", r->str);
        c->disconnect();
        gotCommand = true;
    }, str,3);
    wait();
    ASSERT_TRUE(gotConnect);
    ASSERT_TRUE(gotCommand);
} */

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

    bool gotConnect = false;
    bool gotCommand = false;
    client.onConnect([&](AsyncClient*, bool status) {
        ASSERT_TRUE(status);
        gotConnect = true;
    });

    client.cmd([&](AsyncClient *c, redisReply *r) {
        ASSERT_TRUE(r != NULL);
        ASSERT_EQ(REDIS_REPLY_STATUS, r->type);
        ASSERT_STREQ("OK", r->str);
        c->disconnect();
        gotCommand = true;
    }, "SET %s %s","foo", "hello world");
    wait();
    ASSERT_TRUE(gotConnect);
    ASSERT_TRUE(gotCommand);
}
