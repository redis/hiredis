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

#ifndef __HIREDIS_H
#define __HIREDIS_H

#define REDIS_ERR -1
#define REDIS_OK 0

/* Connection type can be blocking or non-blocking and is set in the
 * least significant bit of the flags field in redisContext. */
#define REDIS_BLOCK 0x1

#define REDIS_ERROR -1
#define REDIS_REPLY_ERROR 0
#define REDIS_REPLY_STRING 1
#define REDIS_REPLY_ARRAY 2
#define REDIS_REPLY_INTEGER 3
#define REDIS_REPLY_NIL 4
#define REDIS_REPLY_STATUS 5

#include "sds.h"

/* This is the reply object returned by redisCommand() */
typedef struct redisReply {
    int type; /* REDIS_REPLY_* */
    long long integer; /* The integer when type is REDIS_REPLY_INTEGER */
    char *reply; /* Used for both REDIS_REPLY_ERROR and REDIS_REPLY_STRING */
    size_t elements; /* number of elements, for REDIS_REPLY_ARRAY */
    struct redisReply **element; /* elements vector for REDIS_REPLY_ARRAY */
} redisReply;

typedef struct redisReadTask {
    int type;
    void *parent; /* optional pointer to parent object */
    int idx; /* index in parent (array) object */
} redisReadTask;

typedef struct redisReplyObjectFunctions {
    void *(*createString)(redisReadTask*, char*, size_t);
    void *(*createArray)(redisReadTask*, int);
    void *(*createInteger)(redisReadTask*, long long);
    void *(*createNil)(redisReadTask*);
    void (*freeObject)(void*);
} redisReplyFunctions;

/* Callback prototype */
struct redisContext; /* needs forward declaration of redisContext */
typedef void redisCallbackFn(struct redisContext*, redisReply*, void*);

/* Callback container */
typedef struct redisCallback {
    redisCallbackFn *fn;
    void *privdata;
} redisCallback;

/* Context for a connection to Redis */
typedef struct redisContext {
    int fd;
    int flags;
    sds error; /* Error object is set when in erronous state */
    sds obuf; /* Write buffer */
    redisReplyFunctions *fn;
    void *reader;
    redisCallback *callbacks;
    int cpos;
    int clen;
} redisContext;

void freeReplyObject(void *reply);
void *redisReplyReaderCreate(redisReplyFunctions *fn);
void *redisReplyReaderGetObject(void *reader);
char *redisReplyReaderGetError(void *reader);
void redisReplyReaderFree(void *ptr);
void redisReplyReaderFeed(void *reader, char *buf, int len);
int redisReplyReaderGetReply(void *reader, void **reply);

redisContext *redisConnect(const char *ip, int port, redisReplyFunctions *fn);
redisContext *redisConnectNonBlock(const char *ip, int port, redisReplyFunctions *fn);
void redisFree(redisContext *c);
int redisBufferRead(redisContext *c);
int redisBufferWrite(redisContext *c, int *done);
int redisGetReply(redisContext *c, void **reply);
int redisProcessCallbacks(redisContext *c);

void *redisCommand(redisContext *c, const char *format, ...);
void *redisCommandWithCallback(redisContext *c, redisCallbackFn *fn, void *privdata, const char *format, ...);

#endif
