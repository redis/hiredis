#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <errno.h>

/* local */
#include "context.h"
#include "format.h"
#include "object.h"

static void redis__clear_address(redis_context *ctx) {
    memset(&ctx->address, 0, sizeof(ctx->address));
}

int redis_context_init(redis_context *ctx) {
    int rv;

    rv = redis_handle_init(&ctx->handle);
    if (rv != REDIS_OK) {
        return rv;
    }

    /* Pull default timeout from handle */
    ctx->timeout = redis_handle_get_timeout(&ctx->handle);

    /* Reinitialize parser with bundled object functions */
    redis_parser_init(&ctx->handle.parser, &redis_object_parser_callbacks);

    /* Clear address field */
    redis__clear_address(ctx);

    return REDIS_OK;
}

int redis_context_destroy(redis_context *ctx) {
    int rv;

    rv = redis_handle_destroy(&ctx->handle);
    return rv;
}

int redis_context_set_timeout(redis_context *ctx, unsigned long timeout) {
    int rv;

    rv = redis_handle_set_timeout(&ctx->handle, timeout);
    if (rv == REDIS_OK) {
        ctx->timeout = timeout;
    }

    return rv;
}

unsigned long redis_context_get_timeout(redis_context *ctx) {
    return ctx->timeout;
}

static int redis__connect(redis_context *ctx) {
    int rv;

    rv = redis_handle_connect_address(&ctx->handle, &ctx->address);
    if (rv != REDIS_OK) {
        return rv;
    }

    rv = redis_handle_wait_connected(&ctx->handle);
    if (rv != REDIS_OK) {
        return rv;
    }

    return REDIS_OK;
}

static int redis__first_connect(redis_context *ctx) {
    int rv;

    rv = redis__connect(ctx);
    if (rv != REDIS_OK) {
        redis__clear_address(ctx);
        return rv;
    }

    return REDIS_OK;
}

int redis_context_connect_in(redis_context *ctx, struct sockaddr_in addr) {
    ctx->address.sa_family = AF_INET;
    ctx->address.sa_addrlen = sizeof(addr);
    ctx->address.sa_addr.in = addr;
    return redis__first_connect(ctx);
}

int redis_context_connect_in6(redis_context *ctx, struct sockaddr_in6 addr) {
    ctx->address.sa_family = AF_INET6;
    ctx->address.sa_addrlen = sizeof(addr);
    ctx->address.sa_addr.in6 = addr;
    return redis__first_connect(ctx);
}

int redis_context_connect_un(redis_context *ctx, struct sockaddr_un addr) {
    ctx->address.sa_family = AF_LOCAL;
    ctx->address.sa_addrlen = sizeof(addr);
    ctx->address.sa_addr.un = addr;
    return redis__first_connect(ctx);
}

int redis_context_connect_gai(redis_context *ctx, const char *host, int port) {
    redis_address address;
    int rv;

    rv = redis_handle_connect_gai(&ctx->handle, AF_INET, host, port, &address);
    if (rv != REDIS_OK) {
        return rv;
    }

    rv = redis_handle_wait_connected(&ctx->handle);
    if (rv != REDIS_OK) {
        return rv;
    }

    /* Store address that getaddrinfo resolved and we are now connected to */
    ctx->address = address;

    return REDIS_OK;
}

int redis_context_flush(redis_context *ctx) {
    int rv;
    int drained = 0;

    while (!drained) {
        rv = redis_handle_write_from_buffer(&ctx->handle, &drained);

        if (rv == REDIS_OK) {
            continue;
        }

        /* Wait for the socket to become writable on EAGAIN */
        if (rv == REDIS_ESYS && errno == EAGAIN) {
            rv = redis_handle_wait_writable(&ctx->handle);
            if (rv != REDIS_OK) {
                return rv;
            }

            continue;
        }

        return rv;
    }

    return REDIS_OK;
}

int redis_context_read(redis_context *ctx, redis_protocol **reply) {
    int rv;
    redis_protocol *aux = NULL;

    rv = redis_handle_read_from_buffer(&ctx->handle, &aux);
    if (rv != REDIS_OK) {
        return rv;
    }

    /* No error, no reply: make sure write buffers are flushed */
    if (aux == NULL) {
        rv = redis_context_flush(ctx);
        if (rv != REDIS_OK) {
            return rv;
        }
    }

    while (aux == NULL) {
        rv = redis_handle_wait_readable(&ctx->handle);
        if (rv != REDIS_OK) {
            return rv;
        }

        rv = redis_handle_read_to_buffer(&ctx->handle);
        if (rv != REDIS_OK) {
            return rv;
        }

        rv = redis_handle_read_from_buffer(&ctx->handle, &aux);
        if (rv != REDIS_OK) {
            return rv;
        }
    }

    assert(aux != NULL);
    *reply = aux;
    return REDIS_OK;
}

int redis_context_write_vcommand(redis_context *ctx,
                                 const char *format,
                                 va_list ap)
{
    char *buf;
    int len;
    int rv;

    len = redis_format_vcommand(&buf, format, ap);
    if (len < 0) {
        errno = ENOMEM;
        return REDIS_ESYS;
    }

    /* Write command to output buffer in handle */
    rv = redis_handle_write_to_buffer(&ctx->handle, buf, len);
    free(buf);
    return rv;
}

int redis_context_write_command(redis_context *ctx,
                                const char *format,
                                ...)
{
    va_list ap;
    int rv;

    va_start(ap, format);
    rv = redis_context_write_vcommand(ctx, format, ap);
    va_end(ap);
    return rv;
}

int redis_context_write_command_argv(redis_context *ctx,
                                    int argc,
                                    const char **argv,
                                    const size_t *argvlen)
{
    char *buf;
    int len;
    int rv;

    len = redis_format_command_argv(&buf, argc, argv, argvlen);
    if (len < 0) {
        errno = ENOMEM;
        return REDIS_ESYS;
    }

    /* Write command to output buffer in handle */
    rv = redis_handle_write_to_buffer(&ctx->handle, buf, len);
    free(buf);
    return rv;
}

int redis_context_call_vcommand(redis_context *ctx,
                                redis_protocol **reply,
                                const char *format,
                                va_list ap)
{
    int rv;

    rv = redis_context_write_vcommand(ctx, format, ap);
    if (rv != REDIS_OK) {
        return rv;
    }

    return redis_context_read(ctx, reply);
}

int redis_context_call_command(redis_context *ctx,
                               redis_protocol **reply,
                               const char *format,
                               ...)
{
    va_list ap;
    int rv;

    va_start(ap, format);
    rv = redis_context_call_vcommand(ctx, reply, format, ap);
    va_end(ap);
    return rv;
}

int redis_context_call_command_argv(redis_context *ctx,
                                    redis_protocol **reply,
                                    int argc,
                                    const char **argv,
                                    const size_t *argvlen)
{
    int rv;

    rv = redis_context_write_command_argv(ctx, argc, argv, argvlen);
    if (rv != REDIS_OK) {
        return rv;
    }

    return redis_context_read(ctx, reply);
}
