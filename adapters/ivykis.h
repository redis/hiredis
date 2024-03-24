
/*
 * Copyright (c) 2013, Gergely Nagy <algernon at madhouse-project dot org>
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


#ifndef __HIREDIS_IVYKIS_H__
#define __HIREDIS_IVYKIS_H__
#include <iv.h>
#include "../hiredis.h"
#include "../async.h"

typedef struct redisIvykisEvents {
    redisAsyncContext *context;
    struct iv_fd fd;
} redisIvykisEvents;

static void redisIvykisReadEvent(void *arg) {
    redisAsyncContext *context = (redisAsyncContext *)arg;
    redisAsyncHandleRead(context);
}

static void redisIvykisWriteEvent(void *arg) {
    redisAsyncContext *context = (redisAsyncContext *)arg;
    redisAsyncHandleWrite(context);
}

static void redisIvykisAddRead(void *privdata) {
    redisIvykisEvents *e = (redisIvykisEvents*)privdata;
    iv_fd_set_handler_in(&e->fd, redisIvykisReadEvent);
}

static void redisIvykisDelRead(void *privdata) {
    redisIvykisEvents *e = (redisIvykisEvents*)privdata;
    iv_fd_set_handler_in(&e->fd, NULL);
}

static void redisIvykisAddWrite(void *privdata) {
    redisIvykisEvents *e = (redisIvykisEvents*)privdata;
    iv_fd_set_handler_out(&e->fd, redisIvykisWriteEvent);
}

static void redisIvykisDelWrite(void *privdata) {
    redisIvykisEvents *e = (redisIvykisEvents*)privdata;
    iv_fd_set_handler_out(&e->fd, NULL);
}

static void redisIvykisCleanup(void *privdata) {
    redisIvykisEvents *e = (redisIvykisEvents*)privdata;

    iv_fd_unregister(&e->fd);
    hi_free(e);
}

static int redisIvykisAttach(redisAsyncContext *ac) {
    redisContext *c = &(ac->c);
    redisIvykisEvents *e;

    /* Nothing should be attached when something is already attached */
    if (ac->ev.data != NULL)
        return REDIS_ERR;

    /* Create container for context and r/w events */
    e = (redisIvykisEvents*)hi_malloc(sizeof(*e));
    if (e == NULL)
        return REDIS_ERR;

    e->context = ac;

    /* Register functions to start/stop listening for events */
    ac->ev.addRead = redisIvykisAddRead;
    ac->ev.delRead = redisIvykisDelRead;
    ac->ev.addWrite = redisIvykisAddWrite;
    ac->ev.delWrite = redisIvykisDelWrite;
    ac->ev.cleanup = redisIvykisCleanup;
    ac->ev.data = e;

    /* Initialize and install read/write events */
    IV_FD_INIT(&e->fd);
    e->fd.fd = c->fd;
    e->fd.handler_in = redisIvykisReadEvent;
    e->fd.handler_out = redisIvykisWriteEvent;
    e->fd.handler_err = NULL;
    e->fd.cookie = e->context;

    iv_fd_register(&e->fd);

    return REDIS_OK;
}
#endif
