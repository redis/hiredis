#ifndef _HIREDIS_HANDLE_H
#define _HIREDIS_HANDLE_H 1

#include <sys/time.h>

#include "address.h"
#include "common.h"
#include "parser.h"

typedef struct redis_handle_s redis_handle;

struct redis_handle_s {
    int fd;
    struct timeval timeout;
    redis_parser parser;
    char *wbuf;
    char *rbuf;
};

int redis_handle_init(redis_handle *h);
int redis_handle_close(redis_handle *h);
int redis_handle_destroy(redis_handle *h);
int redis_handle_set_timeout(redis_handle *h, unsigned long us);
unsigned long redis_handle_get_timeout(redis_handle *h);

int redis_handle_connect_address(redis_handle *h, const redis_address addr);
int redis_handle_connect_in(redis_handle *h, struct sockaddr_in sa);
int redis_handle_connect_in6(redis_handle *h, struct sockaddr_in6 sa);
int redis_handle_connect_un(redis_handle *h, struct sockaddr_un sa);
int redis_handle_connect_gai(redis_handle *h, int family, const char *host, int port, redis_address *addr);

int redis_handle_wait_connected(redis_handle *h);
int redis_handle_wait_readable(redis_handle *h);
int redis_handle_wait_writable(redis_handle *h);

int redis_handle_write_from_buffer(redis_handle *h, int *drained);
int redis_handle_write_to_buffer(redis_handle *h, const char *buf, size_t len);

int redis_handle_read_to_buffer(redis_handle *h);
int redis_handle_read_from_buffer(redis_handle *h, redis_protocol **p);

#endif
