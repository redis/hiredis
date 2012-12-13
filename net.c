/* Extracted from anet.c to work properly with Hiredis error reporting.
 *
 * Copyright (c) 2006-2011, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2010-2011, Pieter Noordhuis <pcnoordhuis at gmail dot com>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "fmacros.h"
#ifndef _WIN32
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <netdb.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <poll.h>
#include <limits.h>
#else
#include <ws2tcpip.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <limits.h>
int
strerror_r(int errnum, char *buf, size_t buflen) {
    if (strncpy(buf, strerror(errnum), buflen) > buflen) {
        return ERANGE;
    }
    return 0;
}
int
inet_aton(const char *cp, struct in_addr *addr) {
    register unsigned int val;
    register int base, n;
    register char c;
    unsigned int parts[4];
    register unsigned int *pp = parts;

    c = *cp;
    for (;;) {
        if (!isdigit(c))
            return (0);
        val = 0; base = 10;
        if (c == '0') {
            c = *++cp;
            if (c == 'x' || c == 'X')
                base = 16, c = *++cp;
            else
                base = 8;
        }
        for (;;) {
            if (isascii(c) && isdigit(c)) {
                val = (val * base) + (c - '0');
                c = *++cp;
            } else if (base == 16 && isascii(c) && isxdigit(c)) {
                val = (val << 4) |
                    (c + 10 - (islower(c) ? 'a' : 'A'));
                c = *++cp;
            } else
                break;
        }
        if (c == '.') {
            if (pp >= parts + 3)
                return (0);
            *pp++ = val;
            c = *++cp;
        } else
            break;
    }
    if (c != '\0' && (!isascii(c) || !isspace(c)))
        return (0);
    n = pp - parts + 1;
    switch (n) {

    case 0:
        return (0);        /* initial nondigit */

    case 1:                /* a -- 32 bits */
        break;

    case 2:                /* a.b -- 8.24 bits */
        if ((val > 0xffffff) || (parts[0] > 0xff))
            return (0);
        val |= parts[0] << 24;
        break;

    case 3:                /* a.b.c -- 8.8.16 bits */
        if ((val > 0xffff) || (parts[0] > 0xff) || (parts[1] > 0xff))
            return (0);
        val |= (parts[0] << 24) | (parts[1] << 16);
        break;

    case 4:                /* a.b.c.d -- 8.8.8.8 bits */
        if ((val > 0xff) || (parts[0] > 0xff) || (parts[1] > 0xff) || (parts[2] > 0xff))
            return (0);
        val |= (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8);
        break;
    }
    if (addr)
        addr->s_addr = htonl(val);
    return (1);
}
static struct addrinfo*
malloc_ai(int port, u_long addr) {
    struct addrinfo *ai;

    ai = (struct addrinfo*) malloc(sizeof(struct addrinfo) + sizeof(struct sockaddr_in));
    if (ai == NULL)
        return(NULL);

    memset(ai, 0, sizeof(struct addrinfo) + sizeof(struct sockaddr_in));

    ai->ai_addr = (struct sockaddr *)(ai + 1);
    ai->ai_addrlen = sizeof(struct sockaddr_in);
    ai->ai_addr->sa_family = ai->ai_family = AF_INET;

    ((struct sockaddr_in *)(ai)->ai_addr)->sin_port = port;
    ((struct sockaddr_in *)(ai)->ai_addr)->sin_addr.s_addr = addr;

    return(ai);
}

int
getaddrinfo(const char *hostname, const char *servname,
                const struct addrinfo *hints, struct addrinfo **res) {
    struct addrinfo *cur, *prev = NULL;
    struct hostent *hp;
    struct in_addr in;
    int i, port;

    if (servname) {
        struct servent *se;
        if ((se = getservbyname(servname, "tcp")))
            port = se->s_port;
        else
            port = htons(atoi(servname));
    } else
        port = 0;

    if (hints && hints->ai_flags & AI_PASSIVE) {
        if (NULL != (*res = malloc_ai(port, htonl(0x00000000))))
            return 0;
        else
            return EAI_MEMORY;
    }

    if (!hostname) {
        if (NULL != (*res = malloc_ai(port, htonl(0x7f000001))))
            return 0;
        else
            return EAI_MEMORY;
    }

    if (inet_aton(hostname, &in)) {
        if (NULL != (*res = malloc_ai(port, in.s_addr)))
            return 0;
        else
            return EAI_MEMORY;
    }

    hp = gethostbyname(hostname);
    if (hp && hp->h_name && hp->h_name[0] && hp->h_addr_list[0]) {
        for (i = 0; hp->h_addr_list[i]; i++) {
            cur = malloc_ai(port, ((struct in_addr *)hp->h_addr_list[i])->s_addr);
            if (cur == NULL) {
                if (*res)
                    freeaddrinfo(*res);
                return EAI_MEMORY;
            }
            if (prev) prev->ai_next = cur;
            else *res = cur;
            prev = cur;
        }
        return 0;
    }

    return EAI_NODATA;
}
void
freeaddrinfo(struct addrinfo *ai) {
    struct addrinfo *next;

    do {
        next = ai->ai_next;
        free(ai);
    } while (NULL != (ai = next));
}
#define close(x) closesocket(x)
#define EINPROGRESS WSAEINPROGRESS
#define ETIMEDOUT WSAETIMEDOUT
#define EHOSTUNREACH WSAEHOSTUNREACH
#endif

#include "net.h"
#include "sds.h"

/* Defined in hiredis.c */
void __redisSetError(redisContext *c, int type, const char *str);

static void __redisSetErrorFromErrno(redisContext *c, int type, const char *prefix) {
    char buf[128];
    size_t len = 0;

    if (prefix != NULL)
        len = snprintf(buf,sizeof(buf),"%s: ",prefix);
    strerror_r(errno,buf+len,sizeof(buf)-len);
    __redisSetError(c,type,buf);
}

static int redisSetReuseAddr(redisContext *c, int fd) {
    int on = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == -1) {
        __redisSetErrorFromErrno(c,REDIS_ERR_IO,NULL);
        close(fd);
        return REDIS_ERR;
    }
    return REDIS_OK;
}

static int redisCreateSocket(redisContext *c, int type) {
    int s;
    if ((s = socket(type, SOCK_STREAM, 0)) == -1) {
        __redisSetErrorFromErrno(c,REDIS_ERR_IO,NULL);
        return REDIS_ERR;
    }
    if (type == AF_INET) {
        if (redisSetReuseAddr(c,s) == REDIS_ERR) {
            return REDIS_ERR;
        }
    }
    return s;
}

static int redisSetBlocking(redisContext *c, int fd, int blocking) {
    int flags;

    /* Set the socket nonblocking.
     * Note that fcntl(2) for F_GETFL and F_SETFL can't be
     * interrupted by a signal. */
#ifndef _WIN32
    if ((flags = fcntl(fd, F_GETFL)) == -1) {
        __redisSetErrorFromErrno(c,REDIS_ERR_IO,"fcntl(F_GETFL)");
        close(fd);
        return REDIS_ERR;
    }

    if (blocking)
        flags &= ~O_NONBLOCK;
    else
        flags |= O_NONBLOCK;

    if (fcntl(fd, F_SETFL, flags) == -1) {
        __redisSetErrorFromErrno(c,REDIS_ERR_IO,"fcntl(F_SETFL)");
        close(fd);
        return REDIS_ERR;
    }
#else
    DWORD b = (DWORD) blocking;
    //ioctlsocket(fd, FIONBIO, &b);
#endif
    return REDIS_OK;
}

static int redisSetTcpNoDelay(redisContext *c, int fd) {
    int yes = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes)) == -1) {
        __redisSetErrorFromErrno(c,REDIS_ERR_IO,"setsockopt(TCP_NODELAY)");
        close(fd);
        return REDIS_ERR;
    }
    return REDIS_OK;
}

#define __MAX_MSEC (((LONG_MAX) - 999) / 1000)

static int redisContextWaitReady(redisContext *c, int fd, const struct timeval *timeout) {
#ifndef _WIN32
    struct pollfd   wfd[1];
    long msec;

    msec          = -1;
    wfd[0].fd     = fd;
    wfd[0].events = POLLOUT;

    /* Only use timeout when not NULL. */
    if (timeout != NULL) {
        if (timeout->tv_usec > 1000000 || timeout->tv_sec > __MAX_MSEC) {
            close(fd);
            return REDIS_ERR;
        }

        msec = (timeout->tv_sec * 1000) + ((timeout->tv_usec + 999) / 1000);

        if (msec < 0 || msec > INT_MAX) {
            msec = INT_MAX;
        }
    }

    if (errno == EINPROGRESS) {
        int res;

        if ((res = poll(wfd, 1, msec)) == -1) {
            __redisSetErrorFromErrno(c, REDIS_ERR_IO, "poll(2)");
            close(fd);
            return REDIS_ERR;
        } else if (res == 0) {
            errno = ETIMEDOUT;
            __redisSetErrorFromErrno(c,REDIS_ERR_IO,NULL);
            close(fd);
            return REDIS_ERR;
        }

        if (redisCheckSocketError(c, fd) != REDIS_OK)
            return REDIS_ERR;

        return REDIS_OK;
    }

    __redisSetErrorFromErrno(c,REDIS_ERR_IO,NULL);
    close(fd);
#else
    fd_set fds;
    long msec;

    msec          = -1;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    if (timeout != NULL) {
        if (timeout->tv_usec > 1000000 || timeout->tv_sec > __MAX_MSEC) {
            close(fd);
            return REDIS_ERR;
        }

        msec = (timeout->tv_sec * 1000) + ((timeout->tv_usec + 999) / 1000);

        if (msec < 0 || msec > INT_MAX) {
            msec = INT_MAX;
        }
    }

    if (errno == EINPROGRESS) {
        int res;
        struct timeval timeout;
        timeout.tv_usec = msec * 1000;
        if ((res = select(FD_SETSIZE, &fds, NULL, NULL, &timeout)) == -1) {
            __redisSetErrorFromErrno(c, REDIS_ERR_IO, "poll(2)");
            close(fd);
            return REDIS_ERR;
        } else if (res == 0) {
            errno = ETIMEDOUT;
            __redisSetErrorFromErrno(c,REDIS_ERR_IO,NULL);
            close(fd);
            return REDIS_ERR;
        }

        if (redisCheckSocketError(c, fd) != REDIS_OK)
            return REDIS_ERR;

        return REDIS_OK;
    }

    __redisSetErrorFromErrno(c,REDIS_ERR_IO,NULL);
    close(fd);
#endif
    return REDIS_ERR;
}

int redisCheckSocketError(redisContext *c, int fd) {
    int err = 0;
    socklen_t errlen = sizeof(err);

    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen) == -1) {
        __redisSetErrorFromErrno(c,REDIS_ERR_IO,"getsockopt(SO_ERROR)");
        close(fd);
        return REDIS_ERR;
    }

    if (err) {
        errno = err;
        __redisSetErrorFromErrno(c,REDIS_ERR_IO,NULL);
        close(fd);
        return REDIS_ERR;
    }

    return REDIS_OK;
}

int redisContextSetTimeout(redisContext *c, struct timeval tv) {
    if (setsockopt(c->fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv)) == -1) {
        __redisSetErrorFromErrno(c,REDIS_ERR_IO,"setsockopt(SO_RCVTIMEO)");
        return REDIS_ERR;
    }
    if (setsockopt(c->fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv)) == -1) {
        __redisSetErrorFromErrno(c,REDIS_ERR_IO,"setsockopt(SO_SNDTIMEO)");
        return REDIS_ERR;
    }
    return REDIS_OK;
}

int redisContextConnectTcp(redisContext *c, const char *addr, int port, struct timeval *timeout) {
    int s, rv;
    char _port[6];  /* strlen("65535"); */
    struct addrinfo hints, *servinfo, *p;
    int blocking = (c->flags & REDIS_BLOCK);

    snprintf(_port, 6, "%d", port);
    memset(&hints,0,sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(addr,_port,&hints,&servinfo)) != 0) {
        __redisSetError(c,REDIS_ERR_OTHER,gai_strerror(rv));
        return REDIS_ERR;
    }
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((s = socket(p->ai_family,p->ai_socktype,p->ai_protocol)) == -1)
            continue;

        if (redisSetBlocking(c,s,0) != REDIS_OK)
            goto error;
        if (connect(s,p->ai_addr,p->ai_addrlen) == -1) {
            if (errno == EHOSTUNREACH) {
                close(s);
                continue;
            } else if (errno == EINPROGRESS && !blocking) {
                /* This is ok. */
            } else {
                if (redisContextWaitReady(c,s,timeout) != REDIS_OK)
                    goto error;
            }
        }
        if (blocking && redisSetBlocking(c,s,1) != REDIS_OK)
            goto error;
        if (redisSetTcpNoDelay(c,s) != REDIS_OK)
            goto error;

        c->fd = s;
        c->flags |= REDIS_CONNECTED;
        rv = REDIS_OK;
        goto end;
    }
    if (p == NULL) {
        char buf[128];
        snprintf(buf,sizeof(buf),"Can't create socket: %s",strerror(errno));
        __redisSetError(c,REDIS_ERR_OTHER,buf);
        goto error;
    }

error:
    rv = REDIS_ERR;
end:
    freeaddrinfo(servinfo);
    return rv;  // Need to return REDIS_OK if alright
}

int redisContextConnectUnix(redisContext *c, const char *path, struct timeval *timeout) {
#ifndef _WIN32
    int s;
    int blocking = (c->flags & REDIS_BLOCK);
    struct sockaddr_un sa;

    if ((s = redisCreateSocket(c,AF_LOCAL)) < 0)
        return REDIS_ERR;
    if (redisSetBlocking(c,s,0) != REDIS_OK)
        return REDIS_ERR;

    sa.sun_family = AF_LOCAL;
    strncpy(sa.sun_path,path,sizeof(sa.sun_path)-1);
    if (connect(s, (struct sockaddr*)&sa, sizeof(sa)) == -1) {
        if (errno == EINPROGRESS && !blocking) {
            /* This is ok. */
        } else {
            if (redisContextWaitReady(c,s,timeout) != REDIS_OK)
                return REDIS_ERR;
        }
    }

    /* Reset socket to be blocking after connect(2). */
    if (blocking && redisSetBlocking(c,s,1) != REDIS_OK)
        return REDIS_ERR;

    c->fd = s;
    c->flags |= REDIS_CONNECTED;
    return REDIS_OK;
#else
    return REDIS_ERR;
#endif
}
