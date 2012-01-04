#ifndef _HIREDIS_CONTEXT_H
#define _HIREDIS_CONTEXT_H 1

/* local */
#include "handle.h"

typedef struct redis_context_s redis_context;

struct redis_context_s {
    redis_handle handle;
    unsigned long timeout;

    /* private: used to reconnect */
    redis_address address;
};

int redis_context_init(redis_context *ctx);
int redis_context_destroy(redis_context *ctx);
int redis_context_set_timeout(redis_context *, unsigned long us);
unsigned long redis_handle_get_timeout(redis_handle *);

int redis_context_connect_in(redis_context *, struct sockaddr_in addr);
int redis_context_connect_in6(redis_context *, struct sockaddr_in6 addr);
int redis_context_connect_un(redis_context *, struct sockaddr_un addr);
int redis_context_connect_gai(redis_context *, const char *addr, int port);

#endif
