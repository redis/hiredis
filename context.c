#include <string.h>
#include <assert.h>

/* local */
#include "context.h"
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
