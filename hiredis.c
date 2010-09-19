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

typedef struct redisReader {
    char *buf; /* read buffer */
    int len; /* buffer length */
    int avail; /* available bytes for consumption */
    int pos; /* buffer cursor */

    redisReply **rlist; /* list of items to process */
    int rlen; /* list length */
    int rpos; /* list cursor */
} redisReader;

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
    redisReply *r = calloc(sizeof(*r),1);

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
        if (r->reply != NULL)
            sdsfree(r->reply);
        break;
    }
    free(r);
}

static redisReply *redisIOError(void) {
    return createReplyObject(REDIS_REPLY_ERROR,sdsnew("I/O error"));
}

static char *readBytes(redisReader *r, int bytes) {
    char *p;
    if (r->len-r->pos >= bytes) {
        p = r->buf+r->pos;
        r->pos += bytes;
        return p;
    }
    return NULL;
}

static char *readLine(redisReader *r, int *_len) {
    char *p, *s = strstr(r->buf+r->pos,"\r\n");
    int len;
    if (s != NULL) {
        p = r->buf+r->pos;
        len = s-(r->buf+r->pos);
        r->pos += len+2; /* skip \r\n */
        if (_len) *_len = len;
        return p;
    }
    return NULL;
}

static int processLineItem(redisReader *r) {
    redisReply *cur = r->rlist[r->rpos];
    char *p;
    int len;

    if ((p = readLine(r,&len)) != NULL) {
        if (cur->type == REDIS_REPLY_INTEGER) {
            cur->integer = strtoll(p,NULL,10);
        } else {
            cur->reply = sdsnewlen(p,len);
        }

        /* for API compat, set STATUS to STRING */
        if (cur->type == REDIS_REPLY_STATUS)
            cur->type = REDIS_REPLY_STRING;

        r->rpos++;
        return 0;
    }
    return -1;
}

static int processBulkItem(redisReader *r) {
    redisReply *cur = r->rlist[r->rpos];
    char *p;
    int len;

    if (cur->reply == NULL) {
        if ((p = readLine(r,NULL)) != NULL) {
            len = atoi(p);
            if (len == -1) {
                /* nil means this item is done */
                cur->type = REDIS_REPLY_NIL;
                cur->reply = sdsempty();
                r->rpos++;
                return 0;
            } else {
                cur->reply = sdsnewlen(NULL,len);
            }
        } else {
            return -1;
        }
    }

    len = sdslen(cur->reply);
    /* add two bytes for crlf */
    if ((p = readBytes(r,len+2)) != NULL) {
        memcpy(cur->reply,p,len);
        r->rpos++;
        return 0;
    }
    return -1;
}

static int processMultiBulkItem(redisReader *r) {
    redisReply *cur = r->rlist[r->rpos];
    char *p;
    int elements, j;

    if ((p = readLine(r,NULL)) != NULL) {
        elements = atoi(p);
        if (elements == -1) {
            /* empty */
            cur->type = REDIS_REPLY_NIL;
            cur->reply = sdsempty();
            r->rpos++;
            return 0;
        }
    } else {
        return -1;
    }

    cur->elements = elements;
    r->rlen += elements;
    r->rpos++;

    /* create placeholder items */
    if ((cur->element = malloc(sizeof(redisReply*)*elements)) == NULL)
        redisOOM();
    if ((r->rlist = realloc(r->rlist,sizeof(redisReply*)*r->rlen)) == NULL)
        redisOOM();

    /* move existing items backwards */
    memmove(&(r->rlist[r->rpos+elements]),
            &(r->rlist[r->rpos]),
            (r->rlen-(r->rpos+elements))*sizeof(redisReply*));

    /* populate item list */
    for (j = 0; j < elements; j++) {
        cur->element[j] = createReplyObject(-1,NULL);
        r->rlist[r->rpos+j] = cur->element[j];
    }
    return 0;
}

static int processItem(redisReader *r) {
    redisReply *cur = r->rlist[r->rpos];
    char *p;

    /* check if we need to read type */
    if (cur->type < 0) {
        if ((p = readBytes(r,1)) != NULL) {
            switch (p[0]) {
            case '-':
                cur->type = REDIS_REPLY_ERROR;
                break;
            case '+':
                cur->type = REDIS_REPLY_STATUS;
                break;
            case ':':
                cur->type = REDIS_REPLY_INTEGER;
                break;
            case '$':
                cur->type = REDIS_REPLY_STRING;
                break;
            case '*':
                cur->type = REDIS_REPLY_ARRAY;
                break;
            default:
                printf("protocol error, got '%c' as reply type byte\n", p[0]);
                exit(1);
            }
        } else {
            /* could not consume 1 byte */
            return -1;
        }
    }

    /* process typed item */
    switch(cur->type) {
    case REDIS_REPLY_ERROR:
    case REDIS_REPLY_STATUS:
    case REDIS_REPLY_INTEGER:
        return processLineItem(r);
    case REDIS_REPLY_STRING:
        return processBulkItem(r);
    case REDIS_REPLY_ARRAY:
        return processMultiBulkItem(r);
    default:
        printf("unknown item type: %d\n", cur->type);
        exit(1);
    }
}

#define READ_BUFFER_SIZE 2048
static redisReply *redisReadReply(int fd) {
    redisReader r;
    int bytes;

    /* setup read buffer */
    r.buf = malloc(READ_BUFFER_SIZE+1);
    r.len = READ_BUFFER_SIZE;
    r.avail = 0;
    r.pos = 0;

    /* setup list of items to process */
    r.rlist = malloc(sizeof(redisReply*));
    r.rlist[0] = createReplyObject(-1,NULL);
    r.rlen = 1;
    r.rpos = 0;

    while (r.rpos < r.rlen) {
        /* discard the buffer upto pos */
        if (r.pos > 0) {
            memmove(r.buf,r.buf+r.pos,r.len-r.pos);
            r.avail -= r.pos;
            r.pos = 0;
        }

        /* make sure there is room for at least BUFFER_SIZE */
        if (r.len-r.avail < READ_BUFFER_SIZE) {
            r.buf = realloc(r.buf,r.avail+READ_BUFFER_SIZE+1);
            r.len = r.avail+READ_BUFFER_SIZE;
        }

        /* read from socket into buffer */
        if ((bytes = read(fd,r.buf+r.avail,READ_BUFFER_SIZE)) <= 0) {
            /* rlist[0] is the "root" reply object */
            freeReplyObject(r.rlist[0]);
            free(r.buf);
            free(r.rlist);
            return redisIOError();
        }
        r.avail += bytes;
        r.buf[r.avail] = '\0';

        /* process items in reply */
        while (r.rpos < r.rlen)
            if (processItem(&r) < 0)
                break;
    }
    free(r.buf);
    free(r.rlist);
    return r.rlist[0];
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
