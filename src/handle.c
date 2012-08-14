#include "fmacros.h"

#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fd.h"
#include "handle.h"
#include "sds.h"

#define REDIS__READABLE 1
#define REDIS__WRITABLE 2

int redis_handle_init(redis_handle *h) {
    h->fd = -1;
    h->timeout.tv_sec = 5;
    h->timeout.tv_usec = 0;
    redis_parser_init(&h->parser, NULL);
    h->wbuf = NULL;
    h->rbuf = NULL;
    return REDIS_OK;
}

/* Associate timeout with handle. Only used in redis_handle_wait_* calls. */
int redis_handle_set_timeout(redis_handle *h, unsigned long us) {
    struct timeval to;
    to.tv_sec = us / 1000000;
    to.tv_usec = us - (1000000 * to.tv_sec);
    h->timeout = to;
    return REDIS_OK;
}

unsigned long redis_handle_get_timeout(redis_handle *h) {
    return h->timeout.tv_sec * 1000000 + h->timeout.tv_usec;
}

int redis_handle_close(redis_handle *h) {
    if (h->fd >= 0) {
        close(h->fd);
        h->fd = -1;
    }

    if (h->wbuf) {
        sdsfree(h->wbuf);
        h->wbuf = NULL;
    }

    if (h->rbuf) {
        sdsfree(h->rbuf);
        h->rbuf = NULL;
    }

    return REDIS_OK;
}

int redis_handle_destroy(redis_handle *h) {
    redis_handle_close(h);
    redis_parser_destroy(&h->parser);
    return REDIS_OK;
}

static int redis__finish_connect(redis_handle *h, int fd) {
    h->fd = fd;
    h->wbuf = sdsempty();
    h->rbuf = sdsempty();
    return REDIS_OK;
}

int redis_handle_connect_address(redis_handle *h, const redis_address addr) {
    int fd;

    if (h->fd >= 0) {
        errno = EALREADY;
        return REDIS_ESYS;
    }

    fd = redis_fd_connect_address(addr);
    if (fd < 0) {
        return fd;
    }

    return redis__finish_connect(h, fd);
}

int redis_handle_connect_in(redis_handle *h, struct sockaddr_in sa) {
    return redis_handle_connect_address(h, redis_address_from_in(sa));
}

int redis_handle_connect_in6(redis_handle *h, struct sockaddr_in6 sa) {
    return redis_handle_connect_address(h, redis_address_from_in6(sa));
}

int redis_handle_connect_un(redis_handle *h, struct sockaddr_un sa) {
    return redis_handle_connect_address(h, redis_address_from_un(sa));
}

int redis_handle_connect_gai(redis_handle *h,
                             int family,
                             const char *addr,
                             int port,
                             redis_address *_addr) {
    int fd;

    if (h->fd >= 0) {
        errno = EALREADY;
        return REDIS_ESYS;
    }

    fd = redis_fd_connect_gai(family, addr, port, _addr);
    if (fd < 0) {
        return fd;
    }

    return redis__finish_connect(h, fd);
}

static int redis__poll(int mode, int fd, struct timeval timeout) {
    struct pollfd pfd;
    int msec;
    int rv;

    pfd.fd = fd;
    pfd.events = 0;
    pfd.revents = 0;

    if (mode & REDIS__READABLE) {
        pfd.events |= POLLIN;
    }

    if (mode & REDIS__WRITABLE) {
        pfd.events |= POLLOUT;
    }

    msec = (timeout.tv_sec * 1000) + ((timeout.tv_usec + 999) / 1000);
    if (msec < 0) {
        msec = 0;
    }

    rv = poll(&pfd, 1, msec);
    if (rv == -1) {
        return REDIS_ESYS;
    }

    /* No events means poll(2) timed out */
    if (rv == 0) {
        errno = ETIMEDOUT;
        return REDIS_ESYS;
    }

    /* POLLERR is always bad */
    if (pfd.revents & POLLERR) {
        goto error;
    }

    /* POLLHUP is only bad when interested in POLLOUT.
     * When interested in POLLIN, reads can continue until EOF. */
    if ((pfd.revents & POLLHUP) && (pfd.events & POLLOUT)) {
        goto error;
    }

    return REDIS_OK;

error:
    rv = redis_fd_error(fd);
    if (rv < 0) {
        return rv;
    }

    /* We got POLLERR so the socket error MUST be non-zero */
    assert(rv && "Expected socket error");

    /* Act as if the socket error occured */
    errno = rv;
    return REDIS_ESYS;
}

static int redis__wait(redis_handle *h, int mode) {
    int rv;

    if (h->fd < 0) {
        errno = EINVAL;
        return REDIS_ESYS;
    }

    rv = redis__poll(mode, h->fd, h->timeout);
    if (rv < 0) {
        return REDIS_ESYS;
    }

    return REDIS_OK;
}

int redis_handle_wait_connected(redis_handle *h) {
    return redis__wait(h, REDIS__WRITABLE);
}

int redis_handle_wait_readable(redis_handle *h) {
    return redis__wait(h, REDIS__READABLE);
}

int redis_handle_wait_writable(redis_handle *h) {
    return redis__wait(h, REDIS__WRITABLE);
}

int redis_handle_write_from_buffer(redis_handle *h, int *drained) {
    int nwritten;

    if (h->fd < 0) {
        errno = EINVAL;
        return REDIS_ESYS;
    }

    if (sdslen(h->wbuf)) {
        nwritten = redis_fd_write(h->fd, h->wbuf, sdslen(h->wbuf));
        if (nwritten < 0) {
            /* Let all errors bubble, including EAGAIN */
            return nwritten;
        }

        if (nwritten) {
            h->wbuf = sdsrange(h->wbuf, nwritten, -1);
        }
    }

    if (drained) {
        *drained = (sdslen(h->wbuf) == 0);
    }

    return REDIS_OK;
}

int redis_handle_write_to_buffer(redis_handle *h, const char *buf, size_t len) {
    if (h->fd < 0) {
        errno = EINVAL;
        return REDIS_ESYS;
    }

    h->wbuf = sdscatlen(h->wbuf, buf, len);
    return REDIS_OK;
}

int redis_handle_read_to_buffer(redis_handle *h) {
    char buf[2048];
    int nread;

    if (h->fd < 0) {
        errno = EINVAL;
        return REDIS_ESYS;
    }

    nread = redis_fd_read(h->fd, buf, sizeof(buf));
    if (nread < 0) {
        return nread;
    }

    h->rbuf = sdscatlen(h->rbuf, buf, nread);
    return 0;
}

int redis_handle_read_from_buffer(redis_handle *h, redis_protocol **p) {
    size_t navail, nparsed;

    if (h->fd < 0) {
        errno = EINVAL;
        return REDIS_ESYS;
    }

    assert(p != NULL);
    *p = NULL;

    navail = sdslen(h->rbuf);
    if (navail) {
        nparsed = redis_parser_execute(&h->parser, p, h->rbuf, navail);

        /* Trim read buffer */
        h->rbuf = sdsrange(h->rbuf, nparsed, -1);

        /* Test for parse error */
        if (nparsed < navail && *p == NULL) {
            errno = redis_parser_err(&h->parser);
            return REDIS_EPARSER;
        }
    }

    return REDIS_OK;
}
