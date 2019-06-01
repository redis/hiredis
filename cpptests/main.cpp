#include <cstdio>
#include <cstdlib>
#include <tuple>
#include <gtest/gtest.h>

#include "common.h"
#include "redis_fork.h"

using namespace hiredis;

static std::string getKvValue(const std::string& s) {
    size_t n = s.find('=');
    if (n == std::string::npos) {
        return "";
    }
    return s.substr(n+1);
}

static bool isArg(const char *arg, const char *s) {
    return strncasecmp(arg, s, strlen(s)) == 0;
}

int main(int argc, char **argv) {
    ClientSettings* settings = &settings_g;
    redis_server_t redis = { 0 };
    redis.ip = "localhost";
    redis.port = 56379;
    redis.unixsocket = "/tmp/redis.sock";


#ifdef HIREDIS_TEST_SSL_CA
    printf("Setting SSL compile time defaults\n");
    settings->m_ssl_ca_path = HIREDIS_TEST_SSL_CA;
    settings->m_ssl_cert_path = HIREDIS_TEST_SSL_CERT;
    settings->m_ssl_key_path = HIREDIS_TEST_SSL_KEY;
#endif

    for (int ii = 1; ii < argc; ++ii) {
        const char *ss = argv[ii];
        if (isArg(ss, "--unix")) {
            settings->setUnix(getKvValue(ss).c_str());
        } else if (isArg(ss, "--host")) {
            settings->setHost(getKvValue(ss).c_str());
            printf("Set host to %s:%u\n", settings->m_hostname.c_str(), settings->m_port);
        } else if (isArg(ss, "--ssl")) {
            auto v = getKvValue(ss);
            if (v == "0" || v == "false") {
                settings->setSsl(false);
            } else {
                printf("enabling ssl for tests\n");
                settings->setSsl(true);
            }
        }
    }
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
