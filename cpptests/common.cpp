#include "common.h"

hiredis::ClientSettings hiredis::settings_g;

using namespace hiredis;


ClientSettings::ClientSettings() {
    std::string hostval;
    if (getenv("REDIS_SOCKET")) {
        m_hostname.assign(getenv("REDIS_SOCKET"));
        m_mode = REDIS_CONN_UNIX;
        return;
    }

    if (getenv("REDIS_HOST")) {
        hostval.assign(getenv("REDIS_HOST"));
    }
    size_t idx = hostval.find(':');
    if (idx == std::string::npos) {
        // First part is hostname only
        m_hostname = hostval;
    } else {
        m_port = atoi(hostval.c_str() + idx);
        hostval.resize(idx);
    }
    if (!m_port) {
        m_port = 6379;
    }

    // Handle SSL settings as well
    m_ssl_cert_path = getenv("REDIS_SSL_CLIENT_CERT");
    m_ssl_key_path = getenv("REDIS_SSL_CLIENT_KEY");
    m_ssl_ca_path = getenv("REDIS_SSL_CA");
}

void ClientSettings::initOptions(redisOptions& options) const {
    if (m_mode == REDIS_CONN_TCP) {
        REDIS_OPTIONS_SET_TCP(&options, hostname(), port());
    } else if (options.type == REDIS_CONN_UNIX) {
        REDIS_OPTIONS_SET_UNIX(&options, hostname());
    }
}
