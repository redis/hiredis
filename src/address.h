#ifndef _HIREDIS_ADDRESS_H
#define _HIREDIS_ADDRESS_H 1

#include <netinet/in.h>
#include <sys/un.h>

typedef struct redis_address_s redis_address;

struct redis_address_s {
    int sa_family;
    socklen_t sa_addrlen;
    union {
        struct sockaddr addr;
        struct sockaddr_in in;
        struct sockaddr_in6 in6;
        struct sockaddr_un un;
    } sa_addr;
};

redis_address redis_address_from_in(struct sockaddr_in sa);
redis_address redis_address_from_in6(struct sockaddr_in6 sa);
redis_address redis_address_from_un(struct sockaddr_un sa);
redis_address redis_address_in(const char *ip, int port);
redis_address redis_address_in6(const char *ip, int port);
redis_address redis_address_un(const char *path);

#endif
