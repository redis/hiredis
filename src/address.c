#include <string.h>
#include <assert.h>
#include <arpa/inet.h>
#include "address.h"

redis_address redis_address_from_in(struct sockaddr_in sa) {
    redis_address addr;

    memset(&addr, 0, sizeof(addr));
    addr.sa_family = sa.sin_family;
    addr.sa_addrlen = sizeof(sa);
    addr.sa_addr.in = sa;
    return addr;
}

redis_address redis_address_from_in6(struct sockaddr_in6 sa) {
    redis_address addr;

    memset(&addr, 0, sizeof(addr));
    addr.sa_family = sa.sin6_family;
    addr.sa_addrlen = sizeof(sa);
    addr.sa_addr.in6 = sa;
    return addr;
}

redis_address redis_address_from_un(struct sockaddr_un sa) {
    redis_address addr;

    memset(&addr, 0, sizeof(addr));
    addr.sa_family = sa.sun_family;
    addr.sa_addrlen = sizeof(sa);
    addr.sa_addr.un = sa;
    return addr;
}

redis_address redis_address_in(const char *ip, int port) {
    struct sockaddr_in sa;

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    assert(inet_pton(AF_INET, ip, &sa.sin_addr) == 1);
    return redis_address_from_in(sa);
}

redis_address redis_address_in6(const char *ip, int port) {
    struct sockaddr_in6 sa;

    memset(&sa, 0, sizeof(sa));
    sa.sin6_family = AF_INET6;
    sa.sin6_port = htons(port);
    assert(inet_pton(AF_INET6, ip, &sa.sin6_addr) == 1);
    return redis_address_from_in6(sa);
}

redis_address redis_address_un(const char *path) {
    struct sockaddr_un sa;

    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_LOCAL;
    strncpy((char*)&sa.sun_path, path, sizeof(sa.sun_path));
    sa.sun_path[sizeof(sa.sun_path) - 1] = '\0';
    return redis_address_from_un(sa);
}
