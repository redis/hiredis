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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>

#include "hiredis.h"
#include "anet.h"
#include "sds.h"

static redisReply *redisReadReply(int fd);
static redisReply *createReplyObject(int type, sds reply);

/* We simply abort on out of memory */
static void redisOOM(void) {
    fprintf(stderr,"Out of memory in hiredis.c");
    exit(1);
}

/* Connect to a Redis instance. On success NULL is returned and *fd is set
 * to the socket file descriptor. On error a redisReply object is returned
 * with reply->type set to REDIS_REPLY_ERROR and reply->string containing
 * the error message. This replyObject must be freed with redisFreeReply(). */
redisReply *redisConnect(int *fd, const char *ip, int port) {
    char err[ANET_ERR_LEN];

    *fd = anetTcpConnect(err,ip,port);
    if (*fd == ANET_ERR)
        return createReplyObject(REDIS_REPLY_ERROR,sdsnew(err));
    anetTcpNoDelay(NULL,*fd);
    return NULL;
}

/* Create a reply object */
static redisReply *createReplyObject(int type, sds reply) {
    redisReply *r = malloc(sizeof(*r));

    if (!r) redisOOM();
    r->type = type;
    r->reply = reply;
    return r;
}

/* Free a reply object */
void freeReplyObject(redisReply *r) {
    size_t j;

    switch(r->type) {
    case REDIS_REPLY_INTEGER:
        break; /* Nothing to free */
    case REDIS_REPLY_ARRAY:
        for (j = 0; j < r->elements; j++)
            freeReplyObject(r->element[j]);
        free(r->element);
        break;
    default:
        sdsfree(r->reply);
        break;
    }
    free(r);
}

static redisReply *redisIOError(void) {
    return createReplyObject(REDIS_REPLY_ERROR,sdsnew("I/O error"));
}

/* In a real high performance C client this should be bufferized */
static sds redisReadLine(int fd) {
    sds line = sdsempty();

    while(1) {
        char c;
        ssize_t ret;

        ret = read(fd,&c,1);
        if (ret == -1) {
            sdsfree(line);
            return NULL;
        } else if ((ret == 0) || (c == '\n')) {
            break;
        } else {
            line = sdscatlen(line,&c,1);
        }
    }
    return sdstrim(line,"\r\n");
}

static redisReply *redisReadSingleLineReply(int fd, int type) {
    sds buf = redisReadLine(fd);
    
    if (buf == NULL) return redisIOError();
    return createReplyObject(type,buf);
}

static redisReply *redisReadIntegerReply(int fd) {
    sds buf = redisReadLine(fd);
    redisReply *r = malloc(sizeof(*r));

    if (r == NULL) redisOOM();
    if (buf == NULL) return redisIOError();
    r->type = REDIS_REPLY_INTEGER;
    r->integer = strtoll(buf,NULL,10);
    return r;
}

static redisReply *redisReadBulkReply(int fd) {
    sds replylen = redisReadLine(fd);
    sds buf;
    char crlf[2];
    int bulklen;

    if (replylen == NULL) return redisIOError();
    bulklen = atoi(replylen);
    sdsfree(replylen);
    if (bulklen == -1)
        return createReplyObject(REDIS_REPLY_NIL,sdsempty());

    buf = sdsnewlen(NULL,bulklen);
    anetRead(fd,buf,bulklen);
    anetRead(fd,crlf,2);
    return createReplyObject(REDIS_REPLY_STRING,buf);
}

static redisReply *redisReadMultiBulkReply(int fd) {
    sds replylen = redisReadLine(fd);
    long elements, j;
    redisReply *r;

    if (replylen == NULL) return redisIOError();
    elements = strtol(replylen,NULL,10);
    sdsfree(replylen);

    if (elements == -1)
        return createReplyObject(REDIS_REPLY_NIL,sdsempty());

    if ((r = malloc(sizeof(*r))) == NULL) redisOOM();
    r->type = REDIS_REPLY_ARRAY;
    r->elements = elements;
    if ((r->element = malloc(sizeof(*r)*elements)) == NULL) redisOOM();
    for (j = 0; j < elements; j++)
        r->element[j] = redisReadReply(fd);
    return r;
}

static redisReply *redisReadReply(int fd) {
    char type;

    if (anetRead(fd,&type,1) <= 0) return redisIOError();
    switch(type) {
    case '-':
        return redisReadSingleLineReply(fd,REDIS_REPLY_ERROR);
    case '+':
        return redisReadSingleLineReply(fd,REDIS_REPLY_STRING);
    case ':':
        return redisReadIntegerReply(fd);
    case '$':
        return redisReadBulkReply(fd);
    case '*':
        return redisReadMultiBulkReply(fd);
    default:
        printf("protocol error, got '%c' as reply type byte\n", type);
        exit(1);
    }
}

/* Helper function for redisCommand(). It's used to append the next argument
 * to the argument vector. */
static void addArgument(sds a, char ***argv, int *argc) {
    (*argc)++;
    if ((*argv = realloc(*argv, sizeof(char*)*(*argc))) == NULL) redisOOM();
    (*argv)[(*argc)-1] = a;
}

/* Execute a command. This function is printf alike:
 *
 * %s represents a C nul terminated string you want to interpolate
 * %b represents a binary safe string
 *
 * When using %b you need to provide both the pointer to the string
 * and the length in bytes. Examples:
 *
 * redisCommand("GET %s", mykey);
 * redisCommand("SET %s %b", mykey, somevalue, somevalue_len);
 *
 * RETURN VALUE:
 *
 * The returned value is a redisReply object that must be freed using the
 * redisFreeReply() function.
 *
 * given a redisReply "reply" you can test if there was an error in this way:
 *
 * if (reply->type == REDIS_REPLY_ERROR) {
 *     printf("Error in request: %s\n", reply->reply);
 * }
 *
 * The replied string itself is in reply->reply if the reply type is
 * a REDIS_REPLY_STRING. If the reply is a multi bulk reply then
 * reply->type is REDIS_REPLY_ARRAY and you can access all the elements
 * in this way:
 *
 * for (i = 0; i < reply->elements; i++)
 *     printf("%d: %s\n", i, reply->element[i]);
 *
 * Finally when type is REDIS_REPLY_INTEGER the long long integer is
 * stored at reply->integer.
 */
redisReply *redisCommand(int fd, const char *format, ...) {
    va_list ap;
    size_t size;
    const char *arg, *c = format;
    sds cmd = sdsempty();     /* whole command buffer */
    sds current = sdsempty(); /* current argument */
    char **argv = NULL;
    int argc = 0, j;

    /* Build the command string accordingly to protocol */
    va_start(ap,format);
    while(*c != '\0') {
        if (*c != '%' || c[1] == '\0') {
            if (*c == ' ') {
                if (sdslen(current) != 0) {
                    addArgument(current, &argv, &argc);
                    current = sdsempty();
                }
            } else {
                current = sdscatlen(current,c,1);
            }
        } else {
            switch(c[1]) {
            case 's':
                arg = va_arg(ap,char*);
                current = sdscat(current,arg);
                break;
            case 'b':
                arg = va_arg(ap,char*);
                size = va_arg(ap,size_t);
                current = sdscatlen(current,arg,size);
                break;
            case '%':
                cmd = sdscat(cmd,"%");
                break;
            }
            c++;
        }
        c++;
    }
    va_end(ap);

    /* Add the last argument if needed */
    if (sdslen(current) != 0)
        addArgument(current, &argv, &argc);
    else
        sdsfree(current);

    /* Build the command at protocol level */
    cmd = sdscatprintf(cmd,"*%d\r\n",argc);
    for (j = 0; j < argc; j++) {
        cmd = sdscatprintf(cmd,"$%zu\r\n",sdslen(argv[j]));
        cmd = sdscatlen(cmd,argv[j],sdslen(argv[j]));
        cmd = sdscatlen(cmd,"\r\n",2);
        sdsfree(argv[j]);
    }
    free(argv);

    /* Send the command via socket */
    anetWrite(fd,cmd,sdslen(cmd));
    sdsfree(cmd);
    return redisReadReply(fd);
}
