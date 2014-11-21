/*
 * Copyright (c) 2014, Anthony Catel <paraboul at gmail dot com>
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

#ifndef __HIREDIS_LIBAPENETWORK_H__
#define __HIREDIS_LIBAPENETWORK_H__
#include <native_netlib.h>
#include "../hiredis.h"
#include "../async.h"

typedef struct _redis_socket {
    _APE_FD_DELEGATE_TPL
} redis_socket;


static void redisLibapenetworkAddRead(void *privdata)
{
    redis_socket *socket = (redis_socket *)privdata;
}

static void redisLibapenetworkDelRead(void *privdata)
{
    redis_socket *socket = (redis_socket *)privdata;
}

static void redisLibapenetworkAddWrite(void *privdata)
{
    redis_socket *socket = (redis_socket *)privdata;
}

static void redisLibapenetworkDelWrite(void *privdata)
{
    redis_socket *socket = (redis_socket *)privdata;
}

static void redisLibapenetworkCleanup(void *privdata)
{
    redis_socket *socket = (redis_socket *)privdata;

    free(socket);
}

static void ape_redis_io(int fd, int ev, void *data, ape_global *ape)
{
    redisAsyncContext *c = (redisAsyncContext *)data;

    if (ev & EVENT_READ) {
        redisAsyncHandleRead(c);
    }
    if (ev & EVENT_WRITE) {
        redisAsyncHandleWrite(c);
    }
}

static int redisLibapenetworkAttach(redisAsyncContext *ac, ape_global *ape) {

    redisContext *c = &(ac->c);

    /* Nothing should be attached when something is already attached */
    if (ac->ev.data != NULL)
        return REDIS_ERR;

    redis_socket *socket = (redis_socket *)malloc(sizeof(*socket));

    socket->s.fd   = c->fd;
    socket->s.type = APE_DELEGATE;
    socket->on_io  = ape_redis_io;
    socket->data   = ac;

    /* Register functions to start/stop listening for events */
    ac->ev.addRead  = redisLibapenetworkAddRead;
    ac->ev.delRead  = redisLibapenetworkDelRead;
    ac->ev.addWrite = redisLibapenetworkAddWrite;
    ac->ev.delWrite = redisLibapenetworkDelWrite;
    ac->ev.cleanup  = redisLibapenetworkCleanup;
    ac->ev.data = socket;

    /* Initialize and install read/write events */
    events_add(c->fd, socket, EVENT_READ|EVENT_WRITE|EVENT_LEVEL, ape);

    return REDIS_OK;
}
#endif
