#ifndef REDIS_SSLIO_H
#define REDIS_SSLIO_H


#ifndef HIREDIS_SSL
typedef struct redisSsl {
    size_t lastLen;
    int wantRead;
    int pendingWrite;
} redisSsl;
static inline void redisFreeSsl(redisSsl *ssl) {
    (void)ssl;
}
static inline int redisSslCreate(struct redisContext *c, const char *ca,
                          const char *cert, const char *key, const char *servername) {
    (void)c;(void)ca;(void)cert;(void)key;(void)servername;
    return REDIS_ERR;
}
static inline int redisSslRead(struct redisContext *c, char *s, size_t n) {
    (void)c;(void)s;(void)n;
    return -1;
}
static inline int redisSslWrite(struct redisContext *c) {
    (void)c;
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
                   const char *certPath, const char *keyPath, const char *servername);

int redisSslRead(struct redisContext *c, char *buf, size_t bufcap);
int redisSslWrite(struct redisContext *c);

#endif /* HIREDIS_SSL */
#endif /* HIREDIS_SSLIO_H */
