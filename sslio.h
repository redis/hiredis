#ifndef REDIS_SSLIO_H
#define REDIS_SSLIO_H


#ifndef HIREDIS_SSL

/* Dummy struct to satisfy compilation */
typedef struct redisSsl {
    size_t lastLen;
    int wantRead;
    int pendingWrite;
} redisSsl;

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

#endif /* HIREDIS_SSL */

struct redisContext;

void redisFreeSsl(redisSsl *);
int redisSslCreate(struct redisContext *c, const char *caPath,
                   const char *certPath, const char *keyPath, const char *servername);

int redisSslRead(struct redisContext *c, char *buf, size_t bufcap);
int redisSslWrite(struct redisContext *c);

#endif /* HIREDIS_SSLIO_H */
