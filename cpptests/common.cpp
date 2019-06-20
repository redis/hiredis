#include "common.h"
#include <sstream>

hiredis::ClientSettings hiredis::settings_g;

using namespace hiredis;



ClientSettings::ClientSettings(std::string connectType_, std::string str_) {
    if(connectType_ == "env") {
        applyEnv();
    } else if(connectType_ == "tcp") { 
        if(str_ != "") { setHost(str_.c_str()); }
    } else if(connectType_ == "unix") {
        setUnix(str_.c_str());
    } else if(connectType_ == "ssl") {

    }     
}

void ClientSettings::setHost(const char *s) {
    m_mode = REDIS_CONN_TCP;
    std::string hostval(s);
    size_t idx = hostval.find(':');
    if (idx == std::string::npos) {
        // First part is hostname only
        m_hostname = hostval;
    } else {
        m_port = atoi(hostval.c_str() + idx + 1);
        hostval.resize(idx);
    }
    if (!m_port) {
        m_port = 6379;
    }
    m_hostname = hostval;
}

void ClientSettings::setUnix(const char *s) {
    m_mode = REDIS_CONN_UNIX;
    m_hostname = s;
}

void ClientSettings::applyEnv() {
    std::string hostval;
    if (getenv("REDIS_SOCKET")) {
        m_hostname.assign(getenv("REDIS_SOCKET"));
        m_mode = REDIS_CONN_UNIX;
        return;
    }

    if (getenv("REDIS_HOST")) {
        setHost(getenv("REDIS_HOST"));
    }

    // Handle SSL settings as well
    if (getenv("REDIS_SSL_CLIENT_CERT")) {
        m_ssl_cert_path = getenv("REDIS_SSL_CLIENT_CERT");
    }
    if (getenv("REDIS_SSL_CLIENT_KEY")) {
        m_ssl_key_path = getenv("REDIS_SSL_CLIENT_KEY");
    }
    if (getenv("REDIS_SSL_CA")) {
        m_ssl_ca_path = getenv("REDIS_SSL_CA");
    }

}

void ClientSettings::initOptions(redisOptions& options) const {
    if (m_mode == REDIS_CONN_TCP) {
        REDIS_OPTIONS_SET_TCP(&options, hostname(), port());
    } else if (options.type == REDIS_CONN_UNIX) {
        REDIS_OPTIONS_SET_UNIX(&options, hostname());
    }
}


ConnectError::ConnectError(const redisOptions& options) {
    if (options.type == REDIS_CONN_TCP) {
        endpoint = options.endpoint.tcp.ip;
        endpoint += ":";
        endpoint += options.endpoint.tcp.port;
    } else if (options.type == REDIS_CONN_UNIX) {
        endpoint = "unix://";
        endpoint += options.endpoint.unix_socket;
    }
}

void ClientError::throwCode(int code) {
    switch (code) {
    case REDIS_ERR_IO:
        throw IOError();
    case REDIS_ERR_EOF:
        throw IOError("EOF");
    case REDIS_ERR_PROTOCOL:
        throw IOError("Protocol Error");
    case REDIS_ERR_TIMEOUT:
        throw TimeoutError();
    default: {
        std::stringstream ss;
        ss << "unknown error code: ";
        ss << code;
        throw ClientError(ss.str().c_str());
    }
    }
}

void ClientError::throwContext(const redisContext *c) {
//    const char *what;
    switch (c->err) {
    case REDIS_ERR_IO:
    case REDIS_ERR_EOF:
    case REDIS_ERR_PROTOCOL:
        throw IOError(c->errstr);
    case REDIS_ERR_TIMEOUT:
        throw TimeoutError();
    default:
        throw ClientError(c->errstr);
    }
}
