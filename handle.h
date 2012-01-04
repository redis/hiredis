#ifndef _HIREDIS_HANDLE_H
#define _HIREDIS_HANDLE_H 1

/* struct sockaddr_(in|in6|un) */
#include <netinet/in.h>
#include <sys/un.h>

/* struct timeval */
#include <sys/time.h>

/* local */
#include "parser.h"

#define REDIS_OK 0
#define REDIS_ESYS -1
#define REDIS_EGAI -2
#define REDIS_EPARSER -3
#define REDIS_EEOF -4

typedef struct redis_handle_s redis_handle;

struct redis_handle_s {
    int fd;
    struct timeval timeout;
    redis_parser parser;
    char *wbuf;
    char *rbuf;
};

int redis_handle_init(redis_handle *h);
int redis_handle_close(redis_handle *);
int redis_handle_destroy(redis_handle *);
int redis_handle_set_timeout(redis_handle *, unsigned long us);
unsigned long redis_handle_get_timeout(redis_handle *h);

int redis_handle_connect_in(redis_handle *, struct sockaddr_in addr);
int redis_handle_connect_in6(redis_handle *, struct sockaddr_in6 addr);
int redis_handle_connect_un(redis_handle *, struct sockaddr_un addr);
int redis_handle_connect_gai(redis_handle *h, int family, const char *addr, int port);

int redis_handle_wait_connected(redis_handle *);
int redis_handle_wait_readable(redis_handle *);
int redis_handle_wait_writable(redis_handle *);

int redis_handle_write_from_buffer(redis_handle *, int *drained);
int redis_handle_write_to_buffer(redis_handle *, const char *buf, size_t len);

int redis_handle_read_to_buffer(redis_handle *);
int redis_handle_read_from_buffer(redis_handle *, redis_protocol **p);

#endif
