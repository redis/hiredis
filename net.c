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

#include "config.h"
#include "fmacros.h"
#include <sys/types.h>
#ifndef HIREDIS_WIN
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#endif
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#include "net.h"
#include "sds.h"

#ifdef HIREDIS_WIN
#define close closesocket
#define SETERRNO errnox = WSAGetLastError()
#define EINPROGRESS WSAEINPROGRESS
#undef errno
int errnox = EINPROGRESS;
#define errno errnox
#else
#define SETERRNO
#endif

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

static int redisCreateSocket(redisContext *c, int type) {
    int s, on = 1;
    if ((s = socket(type, SOCK_STREAM, 0)) == -1) {
        SETERRNO;
        __redisSetErrorFromErrno(c,REDIS_ERR_IO,NULL);
        return REDIS_ERR;
    }
    if (type == AF_INET) {
        if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char*)&on, sizeof(on)) == -1) {
            SETERRNO;
            __redisSetErrorFromErrno(c,REDIS_ERR_IO,NULL);
            close(s);
            return REDIS_ERR;
        }
    }
    return s;
}

static int redisSetBlocking(redisContext *c, int fd, int blocking) {
#ifdef HIREDIS_WIN
    int iResult;
    int flag;

    if (blocking)
        flag = 0;
    else
        flag = 1;

    iResult = ioctlsocket(fd, FIONBIO, &flag);
    if (iResult != NO_ERROR)
    {
        SETERRNO;
        __redisSetErrorFromErrno(c,REDIS_ERR_IO,"fcntl(F_SETFL)");
        close(fd);
        return REDIS_ERR;
    }
#else
    int flags;

    /* Set the socket nonblocking.
     * Note that fcntl(2) for F_GETFL and F_SETFL can't be
     * interrupted by a signal. */
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
#endif
    return REDIS_OK;
}

static int redisSetTcpNoDelay(redisContext *c, int fd) {
    int yes = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char*)&yes, sizeof(yes)) == -1) {
        SETERRNO;
        __redisSetErrorFromErrno(c,REDIS_ERR_IO,"setsockopt(TCP_NODELAY)");
        close(fd);
        return REDIS_ERR;
    }
    return REDIS_OK;
}

static int redisContextWaitReady(redisContext *c, int fd, const struct timeval *timeout) {
    struct timeval to;
    struct timeval *toptr = NULL;
    fd_set wfd;

    /* Only use timeout when not NULL. */
    if (timeout != NULL) {
        to = *timeout;
        toptr = &to;
    }

#ifdef HIREDIS_WIN
    if (errno == EINPROGRESS || errno == WSAEWOULDBLOCK) {
#else
    if (errno == EINPROGRESS) {
#endif
        FD_ZERO(&wfd);
        FD_SET(fd, &wfd);

        if (select(FD_SETSIZE, NULL, &wfd, NULL, toptr) == -1) {
            SETERRNO;
            __redisSetErrorFromErrno(c,REDIS_ERR_IO,"select(2)");
            close(fd);
            return REDIS_ERR;
        }

        if (!FD_ISSET(fd, &wfd)) {
#ifdef HIREDIS_WIN
            errno = WSAETIMEDOUT;
#else
            errno = ETIMEDOUT;
#endif
            __redisSetErrorFromErrno(c,REDIS_ERR_IO,NULL);
            close(fd);
            return REDIS_ERR;
        }

        return REDIS_OK;
    }

    SETERRNO;
    __redisSetErrorFromErrno(c,REDIS_ERR_IO,NULL);
    close(fd);
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
    if (setsockopt(c->fd,SOL_SOCKET,SO_RCVTIMEO,(char*)&tv,sizeof(tv)) == -1) {
        SETERRNO;
        __redisSetErrorFromErrno(c,REDIS_ERR_IO,"setsockopt(SO_RCVTIMEO)");
        return REDIS_ERR;
    }
    if (setsockopt(c->fd,SOL_SOCKET,SO_SNDTIMEO,(char*)&tv,sizeof(tv)) == -1) {
        SETERRNO;
        __redisSetErrorFromErrno(c,REDIS_ERR_IO,"setsockopt(SO_SNDTIMEO)");
        return REDIS_ERR;
    }
    return REDIS_OK;
}

int redisContextConnectTcp(redisContext *c, const char *addr, int port, struct timeval *timeout) {
    int s;
    int blocking = (c->flags & REDIS_BLOCK);
    struct sockaddr_in sa;

    if ((s = redisCreateSocket(c,AF_INET)) < 0)
        return REDIS_ERR;
    if (redisSetBlocking(c,s,0) != REDIS_OK)
        return REDIS_ERR;

    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    if (inet_pton(AF_INET, addr, &sa.sin_addr) == 0) {
        struct hostent *he;

        he = gethostbyname(addr);
        if (he == NULL) {
            char buf[128];
            snprintf(buf,sizeof(buf),"Can't resolve: %s", addr);
            __redisSetError(c,REDIS_ERR_OTHER,buf);
            close(s);
            return REDIS_ERR;
        }
        memcpy(&sa.sin_addr, he->h_addr, sizeof(struct in_addr));
    }

    if (connect(s, (struct sockaddr*)&sa, sizeof(sa)) == -1) {
        SETERRNO;
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

    if (redisSetTcpNoDelay(c,s) != REDIS_OK)
        return REDIS_ERR;

    c->fd = s;
    c->flags |= REDIS_CONNECTED;
    return REDIS_OK;
}

#ifndef HIREDIS_WIN
int redisContextConnectUnix(redisContext *c, const char *path, struct timeval *timeout) {
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
}
#endif