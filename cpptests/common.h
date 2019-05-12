#ifndef HIREDIS_CPP_COMMON_H
#define HIREDIS_CPP_COMMON_H

#include <string>
#include <cstdlib>
#include <stdexcept>
#include "hiredis.h"

namespace hiredis {
class ClientSettings {
public:
    ClientSettings() {}
    ClientSettings(std::string connectType_, std::string str_);

    void applyEnv();
    void setHost(const char *s);
    void setUnix(const char *s);
    void setSsl(bool v) { m_ssl_enabled = v; }

    const char *ssl_cert() const { return m_ssl_cert_path; }
    const char *ssl_key() const { return m_ssl_key_path; }
    const char *ssl_ca() const { return m_ssl_ca_path; }

    bool is_ssl() const {
        return m_ssl_enabled;
    }

    bool is_unix() const {
        return m_mode == REDIS_CONN_UNIX;
    }

    const char *hostname() const {
        return m_hostname.c_str();
    }
    uint16_t port() const {
        return m_port;
    }
    int mode() const {
        return m_mode;
    }

    void initOptions(redisOptions& options) const;

    std::string m_hostname = "localhost";
    uint16_t m_port = 6379;

    int m_mode = REDIS_CONN_TCP;
    const char *m_ssl_cert_path = NULL;
    const char *m_ssl_ca_path = NULL;
    const char *m_ssl_key_path = NULL;
    bool m_ssl_enabled = false;
};

extern ClientSettings settings_g;

class ClientError : public std::runtime_error {
public:
    ClientError() : std::runtime_error("hiredis error") {
    }
    ClientError(const char *s) : std::runtime_error(s) {
    }
    static void throwCode(int code);
    static void throwContext(const redisContext *ac);
};

class ConnectError : public ClientError {
public:
    ConnectError() : ClientError(){}
    ConnectError(const redisOptions& options);
    virtual const char *what() const noexcept override {
        return endpoint.c_str();
    }
private:
    std::string endpoint;
};

class IOError : public ClientError {
public:
    IOError() : ClientError() {}
    IOError(const char *what) : ClientError(what) {}
};

class TimeoutError : public ClientError {
public:
    TimeoutError() : ClientError("timed out") {}
    TimeoutError(const char *what) : ClientError(what) {}
};

class SSLError : public ClientError {
public:
    SSLError() : ClientError() {}
    SSLError(const char *what) : ClientError(what) {}
};

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


inline redisReply *castReply(void *reply) {
    return reinterpret_cast<redisReply*>(reply);
}

}
#endif
