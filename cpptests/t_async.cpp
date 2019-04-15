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
    AsyncClient(const redisOptions& options, event_base* b) {
        ac = redisAsyncConnectWithOptions(&options);
        redisLibeventAttach(ac, b);
        redisAsyncSetConnectCallback(ac, realConnectCb);
        ac->data = this;
    }

    AsyncClient(const ClientSettings& settings, event_base *b) {
        redisOptions options = { 0 };
        ac = redisAsyncConnectWithOptions(&options);
        redisLibeventAttach(ac, b);
        redisAsyncSetConnectCallback(ac, realConnectCb);
        if (settings.is_ssl()) {
            redisSecureConnection(&ac->c,
                    settings.ssl_ca(), settings.ssl_cert(), settings.ssl_key(),
                    NULL);
        }
        ac->data = this;
    }

    AsyncClient(redisAsyncContext *ac) : ac(ac) {
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

    void cmd(CommandCallback cb, const char *fmt, ...) {
        auto data = new CmdData {this, cb };
        va_list ap;
        va_start(ap, fmt);
        redisvAsyncCommand(ac, realCommandCb, data, fmt, ap);
        va_end(ap);
    }

    void disconnect(DisconnectCallback cb) {
        disconncb = cb;
        redisAsyncDisconnect(ac);
    }

    void disconnect() {
        redisAsyncDisconnect(ac);
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
    redisOptions options = {0};
    struct timeval tv = {0};
    tv.tv_sec = 1;
    options.timeout = &tv;
    settings_g.initOptions(options);
    AsyncClient client(options, libevent);

    client.onConnect([](AsyncClient*, bool status) {
        printf("Status: %d\n", status);
    });

    client.cmd([](AsyncClient *c, redisReply*){
        printf("Got reply!\n");
        c->disconnect();
    }, "PING");

    wait();
}
