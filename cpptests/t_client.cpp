#include <gtest/gtest.h>
#include <cstdlib>
#include <string>
#include "hiredis.h"
#include "common.h"


using namespace hiredis;

class ClientError : public std::runtime_error {
public:
    ClientError() : std::runtime_error("hiredis error") {
    }
    ClientError(const char *s) : std::runtime_error(s) {
    }
};

class ConnectError : public ClientError {
public:
    ConnectError() : ClientError(){}
    ConnectError(const redisOptions& options) {
        if (options.type == REDIS_CONN_TCP) {
            endpoint = options.endpoint.tcp.ip;
            endpoint += ":";
            endpoint += options.endpoint.tcp.port;
        } else if (options.type == REDIS_CONN_UNIX) {
            endpoint = "unix://";
            endpoint += options.endpoint.unix_socket;
        }
    }
    virtual const char *what() const noexcept override{
        return endpoint.c_str();
    }
private:
    std::string endpoint;
};

class IOError : public ClientError {};
class TimeoutError : public ClientError {};
class SSLError : public ClientError {};

class CommandError : public ClientError {
public:
    CommandError(const redisReply *r) {
        errstr = r->str;
    }
    virtual const char *what() const noexcept override {
        return errstr.c_str();
    }
private:
    std::string errstr;
};

void errorFromCode(int code) {
    switch (code) {
    case REDIS_ERR_IO:
    case REDIS_ERR_EOF:
    case REDIS_ERR_PROTOCOL:
        throw IOError();
    case REDIS_ERR_TIMEOUT:
        throw TimeoutError();
    default:
        throw ClientError();
    }
}

class Client {
public:
    operator redisContext*() {
        return ctx;
    }

    Client(redisContext *ctx) :ctx(ctx) {
    }

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
                settings.ssl_key(), NULL) != REDIS_OK) {
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
        return reinterpret_cast<redisReply*>(p);
    }

    void flushdb() {
        redisReply *p = cmd("FLUSHDB");
        if (p == NULL) {
            errorFromCode(ctx->err);
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
        errorFromCode(err);
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

class ClientTest : public ::testing::Test {
};

TEST_F(ClientTest, testTimeout) {
    redisOptions options = {0};
    timeval tv = {0};
    tv.tv_usec = 10000; // 10k micros, small enough
    options.timeout = &tv;
    // see https://tools.ietf.org/html/rfc5737
    // this block of addresses is reserved for "documentation", and it
    // would likely not connect, ever.
    ASSERT_THROW(Client(options).nothing(), ClientError);

    // Test the normal timeout
    Client c(settings_g);
    c.flushdb();
}
