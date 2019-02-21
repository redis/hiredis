#include "hiredis.h"
#include "sslio.h"

#include <assert.h>
#ifdef HIREDIS_SSL
#include <pthread.h>

void __redisSetError(redisContext *c, int type, const char *str);

/**
 * Callback used for debugging
 */
static void sslLogCallback(const SSL *ssl, int where, int ret) {
    const char *retstr = "";
    int should_log = 1;
    /* Ignore low-level SSL stuff */

    if (where & SSL_CB_ALERT) {
        should_log = 1;
    }
    if (where == SSL_CB_HANDSHAKE_START || where == SSL_CB_HANDSHAKE_DONE) {
        should_log = 1;
    }
    if ((where & SSL_CB_EXIT) && ret == 0) {
        should_log = 1;
    }

    if (!should_log) {
        return;
    }

    retstr = SSL_alert_type_string(ret);
    printf("ST(0x%x). %s. R(0x%x)%s\n", where, SSL_state_string_long(ssl), ret, retstr);

    if (where == SSL_CB_HANDSHAKE_DONE) {
        printf("Using SSL version %s. Cipher=%s\n", SSL_get_version(ssl), SSL_get_cipher_name(ssl));
    }
}

typedef pthread_mutex_t sslLockType;
static void sslLockInit(sslLockType *l) {
    pthread_mutex_init(l, NULL);
}
static void sslLockAcquire(sslLockType *l) {
    pthread_mutex_lock(l);
}
static void sslLockRelease(sslLockType *l) {
    pthread_mutex_unlock(l);
}
static pthread_mutex_t *ossl_locks;

static void opensslDoLock(int mode, int lkid, const char *f, int line) {
    sslLockType *l = ossl_locks + lkid;

    if (mode & CRYPTO_LOCK) {
        sslLockAcquire(l);
    } else {
        sslLockRelease(l);
    }

    (void)f;
    (void)line;
}

static void initOpensslLocks(void) {
    unsigned ii, nlocks;
    if (CRYPTO_get_locking_callback() != NULL) {
        /* Someone already set the callback before us. Don't destroy it! */
        return;
    }
    nlocks = CRYPTO_num_locks();
    ossl_locks = malloc(sizeof(*ossl_locks) * nlocks);
    for (ii = 0; ii < nlocks; ii++) {
        sslLockInit(ossl_locks + ii);
    }
    CRYPTO_set_locking_callback(opensslDoLock);
}

void redisFreeSsl(redisSsl *ssl){
    if (ssl->ctx) {
        SSL_CTX_free(ssl->ctx);
    }
    if (ssl->ssl) {
        SSL_free(ssl->ssl);
    }
    free(ssl);
}

int redisSslCreate(redisContext *c, const char *capath, const char *certpath,
                   const char *keypath, const char *servername) {
    assert(!c->ssl);
    c->ssl = calloc(1, sizeof(*c->ssl));
    static int isInit = 0;
    if (!isInit) {
        isInit = 1;
        SSL_library_init();
        initOpensslLocks();
    }

    redisSsl *s = c->ssl;
    s->ctx = SSL_CTX_new(SSLv23_client_method());
    SSL_CTX_set_info_callback(s->ctx, sslLogCallback);
    SSL_CTX_set_mode(s->ctx, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
    SSL_CTX_set_options(s->ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3);
    SSL_CTX_set_verify(s->ctx, SSL_VERIFY_PEER, NULL);

    if ((certpath != NULL && keypath == NULL) || (keypath != NULL && certpath == NULL)) {
        __redisSetError(c, REDIS_ERR, "certpath and keypath must be specified together");
        return REDIS_ERR;
    }

    if (capath) {
        if (!SSL_CTX_load_verify_locations(s->ctx, capath, NULL)) {
            __redisSetError(c, REDIS_ERR, "Invalid CA certificate");
            return REDIS_ERR;
        }
    }
    if (certpath) {
        if (!SSL_CTX_use_certificate_chain_file(s->ctx, certpath)) {
            __redisSetError(c, REDIS_ERR, "Invalid client certificate");
            return REDIS_ERR;
        }
        if (!SSL_CTX_use_PrivateKey_file(s->ctx, keypath, SSL_FILETYPE_PEM)) {
            __redisSetError(c, REDIS_ERR, "Invalid client key");
            return REDIS_ERR;
        }
    }

    s->ssl = SSL_new(s->ctx);
    if (!s->ssl) {
        __redisSetError(c, REDIS_ERR, "Couldn't create new SSL instance");
        return REDIS_ERR;
    }
    if (servername) {
        if (!SSL_set_tlsext_host_name(s->ssl, servername)) {
            __redisSetError(c, REDIS_ERR, "Couldn't set server name indication");
            return REDIS_ERR;
        }
    }

    SSL_set_fd(s->ssl, c->fd);
    SSL_set_connect_state(s->ssl);

    c->flags |= REDIS_SSL;
    int rv = SSL_connect(c->ssl->ssl);
    if (rv == 1) {
        return REDIS_OK;
    }

    rv = SSL_get_error(s->ssl, rv);
    if (((c->flags & REDIS_BLOCK) == 0) &&
        (rv == SSL_ERROR_WANT_READ || rv == SSL_ERROR_WANT_WRITE)) {
        return REDIS_OK;
    }

    if (c->err == 0) {
        __redisSetError(c, REDIS_ERR_IO, "SSL_connect() failed");
    }
    return REDIS_ERR;
}

static int maybeCheckWant(redisSsl *rssl, int rv) {
    /**
     * If the error is WANT_READ or WANT_WRITE, the appropriate flags are set
     * and true is returned. False is returned otherwise
     */
    if (rv == SSL_ERROR_WANT_READ) {
        rssl->wantRead = 1;
        return 1;
    } else if (rv == SSL_ERROR_WANT_WRITE) {
        rssl->pendingWrite = 1;
        return 1;
    } else {
        return 0;
    }
}

int redisSslRead(redisContext *c, char *buf, size_t bufcap) {
    int nread = SSL_read(c->ssl->ssl, buf, bufcap);
    if (nread > 0) {
        return nread;
    } else if (nread == 0) {
        __redisSetError(c, REDIS_ERR_EOF, "Server closed the connection");
        return -1;
    } else {
        int err = SSL_get_error(c->ssl->ssl, nread);
        if (maybeCheckWant(c->ssl, err)) {
            return 0;
        } else {
            __redisSetError(c, REDIS_ERR_IO, NULL);
            return -1;
        }
    }
}

int redisSslWrite(redisContext *c) {
    size_t len = c->ssl->lastLen ? c->ssl->lastLen : sdslen(c->obuf);
    int rv = SSL_write(c->ssl->ssl, c->obuf, len);

    if (rv > 0) {
        c->ssl->lastLen = 0;
    } else if (rv < 0) {
        c->ssl->lastLen = len;

        int err = SSL_get_error(c->ssl->ssl, rv);
        if (maybeCheckWant(c->ssl, err)) {
            return 0;
        } else {
            __redisSetError(c, REDIS_ERR_IO, NULL);
            return -1;
        }
    }
    return rv;
}

#endif
