/*
 * Copyright (c) 2009-2010, Salvatore Sanfilippo <antirez at gmail dot com>
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

#include <string.h>
#include <assert.h>
#include "async.h"
#include "sds.h"
#include "util.h"

static redisAsyncContext *redisAsyncInitialize(redisContext *c) {
    redisAsyncContext *ac = realloc(c,sizeof(redisAsyncContext));
    /* Set all bytes in the async part of the context to 0 */
    memset(ac+sizeof(redisContext),0,sizeof(redisAsyncContext)-sizeof(redisContext));
    return ac;
}

redisAsyncContext *redisAsyncConnect(const char *ip, int port) {
    redisContext *_c = redisConnectNonBlock(ip,port);
    return redisAsyncInitialize(_c);
}

int redisAsyncSetReplyObjectFunctions(redisAsyncContext *ac, redisReplyObjectFunctions *fn) {
    redisContext *c = &(ac->c);
    return redisSetReplyObjectFunctions(c,fn);
}

/* Helper functions to push/shift callbacks */
static void __redisPushCallback(redisCallbackList *list, redisCallback *cb) {
    if (list->head == NULL)
        list->head = cb;
    if (list->tail != NULL)
        list->tail->next = cb;
    list->tail = cb;
}

static redisCallback *__redisShiftCallback(redisCallbackList *list) {
    redisCallback *cb = list->head;
    if (cb != NULL) {
        list->head = cb->next;
        if (cb == list->tail)
            list->tail = NULL;
    }
    return cb;
}

/* This function should be called when the socket is readable.
 * It processes all replies that can be read and executes their callbacks.
 */
void redisAsyncHandleRead(redisAsyncContext *ac) {
    redisContext *c = &(ac->c);
    redisCallback *cb;
    void *reply = NULL;
    int status;

    if (redisBufferRead(c) == REDIS_ERR) {
        // needs error handling
        assert(NULL);
    } else {
        /* Always re-schedule reads */
        if (ac->evAddRead) ac->evAddRead(ac->data);

        while((status = redisGetReply(c,&reply)) == REDIS_OK) {
            /* Abort when there are no more replies */
            if (reply == NULL) break;

            /* Shift callback and execute it */
            cb = __redisShiftCallback(&ac->replies);
            assert(cb != NULL);
            if (cb->fn != NULL) {
                cb->fn(ac,reply,cb->privdata);
            } else {
                c->fn->freeObject(reply);
            }
        }

        // needs error handling
        assert(status == REDIS_OK);
    }
}

void redisAsyncHandleWrite(redisAsyncContext *ac) {
    redisContext *c = &(ac->c);
    int done = 0;

    if (redisBufferWrite(c,&done) == REDIS_ERR) {
        // needs error handling
        assert(NULL);
    } else {
        /* Continue writing when not done, stop writing otherwise */
        if (!done) {
            if (ac->evAddWrite) ac->evAddWrite(ac->data);
        } else {
            if (ac->evDelWrite) ac->evDelWrite(ac->data);
        }

        /* Always schedule reads when something was written */
        if (ac->evAddRead) ac->evAddRead(ac->data);
    }
}

/* Helper function for the redisAsyncCommand* family of functions.
 *
 * Write a formatted command to the output buffer and register the provided
 * callback function with the context.
 */
static int __redisAsyncCommand(redisAsyncContext *ac, redisCallbackFn *fn, void *privdata, char *cmd, size_t len) {
    redisContext *c = &(ac->c);
    redisCallback *cb;
    c->obuf = sdscatlen(c->obuf,cmd,len);

    /* Store callback */
    cb = calloc(1,sizeof(redisCallback));
    if (!cb) redisOOM();
    cb->fn = fn;
    cb->privdata = privdata;
    __redisPushCallback(&(ac->replies),cb);

    /* Always schedule a write when the write buffer is non-empty */
    if (ac->evAddWrite) ac->evAddWrite(ac->data);

    return REDIS_OK;
}

int redisvAsyncCommand(redisAsyncContext *ac, redisCallbackFn *fn, void *privdata, const char *format, va_list ap) {
    char *cmd;
    int len;
    int status;
    len = redisvFormatCommand(&cmd,format,ap);
    status = __redisAsyncCommand(ac,fn,privdata,cmd,len);
    free(cmd);
    return status;
}

int redisAsyncCommand(redisAsyncContext *ac, redisCallbackFn *fn, void *privdata, const char *format, ...) {
    va_list ap;
    int status;
    va_start(ap,format);
    status = redisvAsyncCommand(ac,fn,privdata,format,ap);
    va_end(ap);
    return status;
}

int redisAsyncCommandArgv(redisAsyncContext *ac, redisCallbackFn *fn, void *privdata, int argc, const char **argv, const size_t *argvlen) {
    char *cmd;
    int len;
    int status;
    len = redisFormatCommandArgv(&cmd,argc,argv,argvlen);
    status = __redisAsyncCommand(ac,fn,privdata,cmd,len);
    free(cmd);
    return status;
}
