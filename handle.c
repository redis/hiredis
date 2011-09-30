#include "fmacros.h"

/* misc */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

/* socket/connect/(get|set)sockopt*/
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h> /* TCP_* constants */

/* fcntl */
#include <unistd.h>
#include <fcntl.h>

/* select */
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

/* getaddrinfo */
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

/* local */
#include "handle.h"

#define REDIS__READABLE 1
#define REDIS__WRITABLE 2

int redis_handle_init(redis_handle *h) {
    h->fd = -1;
    h->timeout.tv_sec = 5;
    h->timeout.tv_usec = 0;
    redis_parser_init(&h->parser, NULL);
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

int redis_handle_close(redis_handle *h) {
    if (h->fd >= 0) {
        close(h->fd);
        h->fd = -1;
    }

    return REDIS_OK;
}

int redis_handle_destroy(redis_handle *h) {
    redis_handle_close(h);
    redis_parser_destroy(&h->parser);
    return REDIS_OK;
}

static int redis__nonblock(int fd, int nonblock) {
    int flags;

    if ((flags = fcntl(fd, F_GETFL)) == -1) {
        return -1;
    }

    if (nonblock) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }

    if (fcntl(fd, F_SETFL, flags) == -1) {
        return -1;
    }

    return 0;
}

static int redis__so_error(int fd) {
    int err = 0;
    socklen_t errlen = sizeof(err);

    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen) == -1) {
        return -1;
    }

    return err;
}

static int redis__handle_connect(int family, struct sockaddr *addr, socklen_t addrlen) {
    int fd;
    int on = 1;

    if ((fd = socket(family, SOCK_STREAM, 0)) == -1) {
        return -1;
    }

    if (family == AF_INET || family == AF_INET6) {
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == -1) {
            close(fd);
            return -1;
        }
    }

    /* The socket needs to be non blocking to be able to timeout connect(2). */
    if (redis__nonblock(fd, 1) == -1) {
        close(fd);
        return -1;
    }

    if (connect(fd, addr, addrlen) == -1) {
        if (errno == EINPROGRESS) {
            /* The user should figure out if connect(2) succeeded */
        } else {
            close(fd);
            return -1;
        }
    }

    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on)) == -1) {
        close(fd);
        return -1;
    }

    return fd;
}

int redis_handle_connect_in(redis_handle *h, struct sockaddr_in addr) {
    int fd;

    if (h->fd >= 0) {
        errno = EALREADY;
        return REDIS_ESYS;
    }

    assert(addr.sin_family == AF_INET);
    fd = redis__handle_connect(AF_INET, (struct sockaddr*)&addr, sizeof(addr));
    if (fd == -1) {
        return REDIS_ESYS;
    }

    h->fd = fd;
    return REDIS_OK;
}

int redis_handle_connect_in6(redis_handle *h, struct sockaddr_in6 addr) {
    int fd;

    if (h->fd >= 0) {
        errno = EALREADY;
        return REDIS_ESYS;
    }

    assert(addr.sin6_family == AF_INET6);
    fd = redis__handle_connect(AF_INET6, (struct sockaddr*)&addr, sizeof(addr));
    if (fd == -1) {
        return REDIS_ESYS;
    }

    h->fd = fd;
    return REDIS_OK;
}

int redis_handle_connect_un(redis_handle *h, struct sockaddr_un addr) {
    int fd;

    if (h->fd >= 0) {
        errno = EALREADY;
        return REDIS_ESYS;
    }

    assert(addr.sun_family == AF_LOCAL);
    fd = redis__handle_connect(AF_LOCAL, (struct sockaddr*)&addr, sizeof(addr));
    if (fd == -1) {
        return REDIS_ESYS;
    }

    h->fd = fd;
    return REDIS_OK;
}

int redis_handle_connect_gai(redis_handle *h,
                             int family,
                             const char *addr,
                             int port) {
    char _port[6];  /* strlen("65535"); */
    struct addrinfo hints, *servinfo, *p;
    int rv, fd;

    snprintf(_port, 6, "%d", port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = family;
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(addr, _port, &hints, &servinfo)) != 0) {
        errno = rv;
        return REDIS_EGAI;
    }

    /* Expect at least one record. */
    assert(servinfo != NULL);

    for (p = servinfo; p != NULL; p = p->ai_next) {
        fd = redis__handle_connect(p->ai_family, p->ai_addr, p->ai_addrlen);
        if (fd == -1) {
            if (errno == EHOSTUNREACH) {
                /* AF_INET6 record on a machine without IPv6 support.
                 * See c4ed06d9 for more information. */
                continue;
            }

            goto error;
        }

        freeaddrinfo(servinfo);
        h->fd = fd;
        return REDIS_OK;
    }

error:
    freeaddrinfo(servinfo);
    return REDIS_ESYS;
}

static int redis__select(int mode, int fd, struct timeval timeout) {
    fd_set rfd, wfd;
    fd_set *_set = NULL, *_rfd = NULL, *_wfd = NULL;
    int so_error;

    switch(mode) {
        case REDIS__READABLE:
            FD_ZERO(&rfd);
            FD_SET(fd, &rfd);
            _rfd = _set = &rfd;
            break;
        case REDIS__WRITABLE:
            FD_ZERO(&wfd);
            FD_SET(fd, &wfd);
            _wfd = _set = &wfd;
            break;
        default:
            assert(NULL && "invalid mode");
    }

    if (select(FD_SETSIZE, _rfd, _wfd, NULL, &timeout) == -1) {
        return -1;
    }

    /* Not in set means select(2) timed out */
    if (!FD_ISSET(fd, _set)) {
        errno = ETIMEDOUT;
        return -1;
    }

    /* Check for socket errors. */
    so_error = redis__so_error(fd);
    if (so_error == -1) {
        return -1;
    }

    if (so_error) {
        /* Act as if the socket error occured with select(2). */
        errno = so_error;
        return -1;
    }

    return 0;
}

static int redis__wait(redis_handle *h, int mode) {
    int rv;

    if (h->fd < 0) {
        errno = EINVAL;
        return REDIS_ESYS;
    }

    rv = redis__select(mode, h->fd, h->timeout);
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
