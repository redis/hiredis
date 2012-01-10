#include "fmacros.h"

/* misc */
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

/* getaddrinfo */
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "fd.h"

int redis_fd_error(int fd) {
    int err = 0;
    socklen_t errlen = sizeof(err);

    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen) == -1) {
        return REDIS_ESYS;
    }

    return err;
}

static int redis__nonblock(int fd, int nonblock) {
    int flags;

    if ((flags = fcntl(fd, F_GETFL)) == -1) {
        return REDIS_ESYS;
    }

    if (nonblock) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }

    if (fcntl(fd, F_SETFL, flags) == -1) {
        return REDIS_ESYS;
    }

    return REDIS_OK;
}

static int redis__connect(int family, const struct sockaddr *addr, socklen_t addrlen) {
    int fd, rv;
    int on = 1;

    fd = socket(family, SOCK_STREAM, 0);
    if (fd == -1) {
        goto error;
    }

    if (family == AF_INET || family == AF_INET6) {
        rv = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
        if (rv == -1) {
            goto error;
        }
    }

    /* The socket needs to be non blocking to be able to timeout connect(2). */
    rv = redis__nonblock(fd, 1);
    if (rv != REDIS_OK) {
        goto error;
    }

    rv = connect(fd, addr, addrlen);
    if (rv == -1 && errno != EINPROGRESS) {
        goto error;
    }

    rv = setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
    if (rv == -1) {
        goto error;
    }

    return fd;

error:
    close(fd);
    return REDIS_ESYS;
}

int redis_fd_connect_address(const redis_address addr) {
    return redis__connect(addr.sa_family, &addr.sa_addr.addr, addr.sa_addrlen);
}

int redis_fd_connect_gai(int family,
                         const char *addr,
                         int port,
                         redis_address *_addr)
{
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
        fd = redis__connect(p->ai_family, p->ai_addr, p->ai_addrlen);
        if (fd == -1) {
            if (errno == EHOSTUNREACH) {
                /* AF_INET6 record on a machine without IPv6 support.
                 * See c4ed06d9 for more information. */
                continue;
            }

            goto error;
        }

        /* Pass address we connect to back to caller */
        if (_addr != NULL) {
            memset(_addr, 0, sizeof(*_addr));
            memcpy(&_addr->sa_addr, p->ai_addr, p->ai_addrlen);
            _addr->sa_family = p->ai_family;
            _addr->sa_addrlen = p->ai_addrlen;
        }

        freeaddrinfo(servinfo);
        return fd;
    }

error:
    freeaddrinfo(servinfo);
    return REDIS_ESYS;
}
