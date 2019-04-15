#ifndef HIREDIS_CPP_COMMON_H
#define HIREDIS_CPP_COMMON_H

#include <string>
#include <cstdlib>
#include "hiredis.h"

namespace hiredis {
class ClientSettings {
public:
    ClientSettings();

    const char *ssl_cert() const { return m_ssl_cert_path; }
    const char *ssl_key() const { return m_ssl_key_path; }
    const char *ssl_ca() const { return m_ssl_ca_path; }

    bool is_ssl() const {
        return m_ssl_ca_path != NULL;
    }
    bool is_unix() const {
        return false;
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

private:
    std::string m_hostname;
    uint16_t m_port;

    int m_mode = REDIS_CONN_TCP;
    const char *m_ssl_cert_path = NULL;
    const char *m_ssl_ca_path = NULL;
    const char *m_ssl_key_path = NULL;
};

extern ClientSettings settings_g;

}
#endif
