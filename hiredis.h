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

/* Connection may be disconnected before being free'd. The second bit
 * in the flags field is set when the context is connected. */
#define REDIS_CONNECTED 0x2

#define REDIS_ERROR -1
#define REDIS_REPLY_ERROR 0
#define REDIS_REPLY_STRING 1
#define REDIS_REPLY_ARRAY 2
#define REDIS_REPLY_INTEGER 3
#define REDIS_REPLY_NIL 4
#define REDIS_REPLY_STATUS 5

/* This is the reply object returned by redisCommand() */
typedef struct redisReply {
    int type; /* REDIS_REPLY_* */
    long long integer; /* The integer when type is REDIS_REPLY_INTEGER */
    int len; /* Length of string */
    char *str; /* Used for both REDIS_REPLY_ERROR and REDIS_REPLY_STRING */
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
} redisReplyObjectFunctions;

struct redisContext; /* need forward declaration of redisContext */

/* Callbacks triggered on non-reply events. */
typedef void (redisContextCallbackFn)(struct redisContext*, void*);

/* Callback containers */
typedef struct redisContextCallback {
    redisContextCallbackFn *fn;
    void *privdata;
} redisContextCallback;

/* Context for a connection to Redis */
typedef struct redisContext {
    int fd;
    int flags;
    char *error; /* Error object is set when in erronous state */
    char *obuf; /* Write buffer */

    /* Function set for reply buildup and reply reader */
    redisReplyObjectFunctions *fn;
    void *reader;

    /* Non-reply callbacks */
    redisContextCallback cbDisconnect;
    redisContextCallback cbCommand;
    redisContextCallback cbFree;
} redisContext;

void freeReplyObject(void *reply);
void *redisReplyReaderCreate(redisReplyObjectFunctions *fn);
void *redisReplyReaderGetObject(void *reader);
char *redisReplyReaderGetError(void *reader);
void redisReplyReaderFree(void *ptr);
void redisReplyReaderFeed(void *reader, char *buf, int len);
int redisReplyReaderGetReply(void *reader, void **reply);

/* Functions to format a command according to the protocol. */
int redisvFormatCommand(char **target, const char *format, va_list ap);
int redisFormatCommand(char **target, const char *format, ...);
int redisFormatCommandArgv(char **target, int argc, const char **argv, const size_t *argvlen);

redisContext *redisConnect(const char *ip, int port, redisReplyObjectFunctions *fn);
redisContext *redisConnectNonBlock(const char *ip, int port, redisReplyObjectFunctions *fn);
void redisDisconnect(redisContext *c);
void redisFree(redisContext *c);
int redisBufferRead(redisContext *c);
int redisBufferWrite(redisContext *c, int *done);

/* In a blocking context, this function first checks if there are unconsumed
 * replies to return and returns one if so. Otherwise, it flushes the output
 * buffer to the socket and reads until it has a reply. In a non-blocking
 * context, it will return unconsumed replies until there are no more. */
int redisGetReply(redisContext *c, void **reply);

/* The disconnect callback is called *immediately* when redisDisconnect()
 * is called. It is called only once for every redisContext (since hiredis
 * currently does not support reconnecting an existing context). */
void redisSetDisconnectCallback(redisContext *c, redisContextCallbackFn *fn, void *privdata);

/* The command callback is called every time redisCommand() is called in a
 * non-blocking context. It is called *after* the formatted command has been
 * appended to the write buffer. */
void redisSetCommandCallback(redisContext *c, redisContextCallbackFn *fn, void *privdata);

/* The free callback is called *before* all allocations are free'd. Use it to
 * release resources that depend/use the redisContext that is being free'd. */
void redisSetFreeCallback(redisContext *c, redisContextCallbackFn *fn, void *privdata);

/* Write a command to the output buffer. Use these functions in blocking mode
 * to get a pipeline of commands. */
void redisvAppendCommand(redisContext *c, const char *format, va_list ap);
void redisAppendCommand(redisContext *c, const char *format, ...);
void redisAppendCommandArgv(redisContext *c, int argc, const char **argv, const size_t *argvlen);

/* Issue a command to Redis. In a blocking context, it is identical to calling
 * redisAppendCommand, followed by redisGetReply. The function will return
 * NULL if there was an error in performing the request, otherwise it will
 * return the reply. In a non-blocking context, it is identical to calling
 * only redisAppendCommand and will always return NULL. */
void *redisvCommand(redisContext *c, const char *format, va_list ap);
void *redisCommand(redisContext *c, const char *format, ...);
void *redisCommandArgv(redisContext *c, int argc, const char **argv, const size_t *argvlen);

#endif
