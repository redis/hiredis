#ifndef _HIREDIS_CONTEXT_H
#define _HIREDIS_CONTEXT_H 1

#include <stdarg.h>

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
int redis_context_set_timeout(redis_context *ctx, unsigned long us);
unsigned long redis_handle_get_timeout(redis_handle *ctx);

int redis_context_connect_address(redis_context *ctx, const redis_address addr);
int redis_context_connect_in(redis_context *ctx, struct sockaddr_in sa);
int redis_context_connect_in6(redis_context *ctx, struct sockaddr_in6 sa);
int redis_context_connect_un(redis_context *ctx, struct sockaddr_un sa);
int redis_context_connect_gai(redis_context *ctx, const char *addr, int port);

int redis_context_flush(redis_context *ctx);
int redis_context_read(redis_context *ctx, redis_protocol **reply);

int redis_context_write_vcommand(redis_context *ctx,
                                 const char *format,
                                 va_list ap);
int redis_context_write_command(redis_context *ctx,
                                const char *format,
                                ...);
int redis_context_write_command_argv(redis_context *ctx,
                                    int argc,
                                    const char **argv,
                                    const size_t *argvlen);

int redis_context_call_vcommand(redis_context *ctx,
                                redis_protocol **reply,
                                const char *format,
                                va_list ap);
int redis_context_call_command(redis_context *ctx,
                               redis_protocol **reply,
                               const char *format,
                               ...);
int redis_context_call_command_argv(redis_context *ctx,
                                    redis_protocol **reply,
                                    int argc,
                                    const char **argv,
                                    const size_t *argvlen);

#endif
