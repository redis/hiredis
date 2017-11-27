#ifndef REDIS_SSLIO_H
#define REDIS_SSLIO_H


#ifdef HIREDIS_NOSSL
typedef struct redisSsl {
    int dummy;
} redisSsl;
static void redisFreeSsl(redisSsl *) {
}
static int redisSslCreate(struct redisContext *c) {
    return REDIS_ERR;
}
static int redisSslRead(struct redisContect *c, char *s, size_t, n) {
    return -1;
}
static int redisSslWrite(struct redisContext *c) {
    return -1;
}
#else
#include <openssl/ssl.h>

/**
 * This file contains routines for HIREDIS' SSL
 */

typedef struct redisSsl {
    SSL *ssl;
    SSL_CTX *ctx;

    /**
     * SSL_write() requires to be called again with the same arguments it was
     * previously called with in the event of an SSL_read/SSL_write situation
     */
    size_t lastLen;

    /** Whether the SSL layer requires read (possibly before a write) */
    int wantRead;

    /**
     * Whether a write was requested prior to a read. If set, the write()
     * should resume whenever a read takes place, if possible
     */
    int pendingWrite;
} redisSsl;

struct redisContext;

void redisFreeSsl(redisSsl *);
int redisSslCreate(struct redisContext *c, const char *caPath,
                   const char *certPath, const char *keyPath);

int redisSslRead(struct redisContext *c, char *buf, size_t bufcap);
int redisSslWrite(struct redisContext *c);

#endif /* !HIREDIS_NOSSL */
#endif /* HIREDIS_SSLIO_H */
