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
#include <assert.h>

#include "hiredis.h"
#include "anet.h"
#include "sds.h"

typedef struct redisReader {
    void *reply; /* holds temporary reply */

    sds buf; /* read buffer */
    unsigned int pos; /* buffer cursor */

    redisReadTask *rlist; /* list of items to process */
    unsigned int rlen; /* list length */
    unsigned int rpos; /* list cursor */
} redisReader;

static redisReply *redisReadReply(int fd);
static redisReply *createReplyObject(int type, sds reply);
static redisReply *createErrorObject(const char *fmt, ...);
static void redisSetReplyReaderError(redisReader *r, redisReply *error);

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
        return createErrorObject(err);
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
            if (r->element[j]) freeReplyObject(r->element[j]);
        free(r->element);
        break;
    default:
        if (r->reply != NULL)
            sdsfree(r->reply);
        break;
    }
    free(r);
}

static redisReply *createErrorObject(const char *fmt, ...) {
    va_list ap;
    sds err;
    redisReply *r;
    va_start(ap,fmt);
    err = sdscatvprintf(sdsempty(),fmt,ap);
    va_end(ap);
    r = createReplyObject(REDIS_PROTOCOL_ERROR,err);
    return r;
}

static redisReply *redisIOError(void) {
    return createErrorObject("I/O error");
}

static void *createStringObject(redisReadTask *task, char *str, size_t len) {
    redisReply *r = createReplyObject(task->type,sdsnewlen(str,len));
    assert(task->type == REDIS_REPLY_ERROR ||
           task->type == REDIS_REPLY_STATUS ||
           task->type == REDIS_REPLY_STRING);

    /* for API compat, set STATUS to STRING */
    if (task->type == REDIS_REPLY_STATUS)
        r->type = REDIS_REPLY_STRING;

    if (task->parent) {
        redisReply *parent = task->parent;
        assert(parent->type == REDIS_REPLY_ARRAY);
        parent->element[task->idx] = r;
    }
    return r;
}

static void *createArrayObject(redisReadTask *task, int elements) {
    redisReply *r = createReplyObject(REDIS_REPLY_ARRAY,NULL);
    r->elements = elements;
    if ((r->element = calloc(sizeof(redisReply*),elements)) == NULL)
        redisOOM();
    if (task->parent) {
        redisReply *parent = task->parent;
        assert(parent->type == REDIS_REPLY_ARRAY);
        parent->element[task->idx] = r;
    }
    return r;
}

static void *createIntegerObject(redisReadTask *task, long long value) {
    redisReply *r = createReplyObject(REDIS_REPLY_INTEGER,NULL);
    r->integer = value;
    if (task->parent) {
        redisReply *parent = task->parent;
        assert(parent->type == REDIS_REPLY_ARRAY);
        parent->element[task->idx] = r;
    }
    return r;
}

static void *createNilObject(redisReadTask *task) {
    redisReply *r = createReplyObject(REDIS_REPLY_NIL,NULL);
    if (task->parent) {
        redisReply *parent = task->parent;
        assert(parent->type == REDIS_REPLY_ARRAY);
        parent->element[task->idx] = r;
    }
    return r;
}

static char *readBytes(redisReader *r, unsigned int bytes) {
    char *p;
    if (sdslen(r->buf)-r->pos >= bytes) {
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
    redisReadTask *cur = &(r->rlist[r->rpos]);
    void *obj;
    char *p;
    int len;

    if ((p = readLine(r,&len)) != NULL) {
        if (cur->type == REDIS_REPLY_INTEGER) {
            obj = createIntegerObject(cur,strtoll(p,NULL,10));
        } else {
            obj = createStringObject(cur,p,len);
        }

        /* If there is no root yet, register this object as root. */
        if (r->reply == NULL)
            r->reply = obj;
        r->rpos++;
        return 0;
    }
    return -1;
}

static int processBulkItem(redisReader *r) {
    redisReadTask *cur = &(r->rlist[r->rpos]);
    void *obj = NULL;
    char *p, *s;
    long len;
    unsigned long bytelen;

    p = r->buf+r->pos;
    s = strstr(p,"\r\n");
    if (s != NULL) {
        p = r->buf+r->pos;
        bytelen = s-(r->buf+r->pos)+2; /* include \r\n */
        len = strtol(p,NULL,10);

        if (len < 0) {
            /* The nil object can always be created. */
            obj = createNilObject(cur);
        } else {
            /* Only continue when the buffer contains the entire bulk item. */
            bytelen += len+2; /* include \r\n */
            if (r->pos+bytelen <= sdslen(r->buf)) {
                obj = createStringObject(cur,s+2,len);
            }
        }

        /* Proceed when obj was created. */
        if (obj != NULL) {
            r->pos += bytelen;
            if (r->reply == NULL)
                r->reply = obj;
            r->rpos++;
            return 0;
        }
    }
    return -1;
}

static int processMultiBulkItem(redisReader *r) {
    redisReadTask *cur = &(r->rlist[r->rpos]);
    void *obj;
    char *p;
    long elements, j;

    if ((p = readLine(r,NULL)) != NULL) {
        elements = strtol(p,NULL,10);
        if (elements == -1) {
            obj = createNilObject(cur);
        } else {
            obj = createArrayObject(cur,elements);

            /* Modify read list when there are more than 0 elements. */
            if (elements > 0) {
                /* Append elements to the read list. */
                r->rlen += elements;
                if ((r->rlist = realloc(r->rlist,sizeof(redisReadTask)*r->rlen)) == NULL)
                    redisOOM();

                /* Move existing items backwards. */
                memmove(&(r->rlist[r->rpos+1+elements]),
                        &(r->rlist[r->rpos+1]),
                        (r->rlen-(r->rpos+1+elements))*sizeof(redisReadTask));

                /* Populate new read items. */
                redisReadTask *t;
                for (j = 0; j < elements; j++) {
                    t = &(r->rlist[r->rpos+1+j]);
                    t->type = -1;
                    t->parent = obj;
                    t->idx = j;
                }
            }
        }

        if (obj != NULL) {
            if (r->reply == NULL)
                r->reply = obj;
            r->rpos++;
            return 0;
        }
    }
    return -1;
}

static int processItem(redisReader *r) {
    redisReadTask *cur = &(r->rlist[r->rpos]);
    char *p;
    sds byte;

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
                byte = sdscatrepr(sdsempty(),p,1);
                redisSetReplyReaderError(r,createErrorObject(
                    "protocol error, got %s as reply type byte", byte));
                sdsfree(byte);
                return -1;
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
        redisSetReplyReaderError(r,createErrorObject(
            "unknown item type '%d'", cur->type));
        return -1;
    }
}

#define READ_BUFFER_SIZE 2048
static redisReply *redisReadReply(int fd) {
    void *reader = redisCreateReplyReader();
    redisReply *reply;
    char buf[1024];
    int nread;

    do {
        if ((nread = read(fd,buf,sizeof(buf))) <= 0) {
            reply = redisIOError();
            break;
        } else {
            reply = redisFeedReplyReader(reader,buf,nread);
        }
    } while (reply == NULL);

    redisFreeReplyReader(reader);
    return reply;
}

void *redisCreateReplyReader() {
    redisReader *r = calloc(sizeof(redisReader),1);
    r->buf = sdsempty();
    r->rlist = malloc(sizeof(redisReadTask)*1);
    return r;
}

void redisFreeReplyReader(void *reader) {
    redisReader *r = reader;
    if (r->reply != NULL)
        freeReplyObject(r->reply);
    if (r->buf != NULL)
        sdsfree(r->buf);
    if (r->rlist != NULL)
        free(r->rlist);
    free(r);
}

int redisIsReplyReaderEmpty(void *reader) {
    redisReader *r = reader;
    if ((r->buf != NULL && sdslen(r->buf) > 0) ||
        (r->rpos < r->rlen)) return 0;
    return 1;
}

static void redisSetReplyReaderError(redisReader *r, redisReply *error) {
    if (r->reply != NULL)
        freeReplyObject(r->reply);

    /* Clear remaining buffer when we see a protocol error. */
    if (r->buf != NULL) {
        sdsfree(r->buf);
        r->buf = sdsempty();
        r->pos = 0;
    }
    r->rlen = r->rpos = 0;
    r->reply = error;
}

void *redisFeedReplyReader(void *reader, char *buf, int len) {
    redisReader *r = reader;

    /* Check if we are able to do *something*. */
    if (sdslen(r->buf) == 0 && (buf == NULL || len <= 0))
        return NULL;

    /* Copy the provided buffer. */
    if (buf != NULL && len >= 1)
        r->buf = sdscatlen(r->buf,buf,len);

    /* Create first item to process when the item list is empty. */
    if (r->rlen == 0) {
        r->rlist = realloc(r->rlist,sizeof(redisReadTask)*1);
        r->rlist[0].type = -1;
        r->rlist[0].parent = NULL;
        r->rlist[0].idx = -1;
        r->rlen = 1;
        r->rpos = 0;
    }

    /* Process items in reply. */
    while (r->rpos < r->rlen)
        if (processItem(r) < 0)
            break;

    /* Discard the consumed part of the buffer. */
    if (r->pos > 0) {
        if (r->pos == sdslen(r->buf)) {
            /* sdsrange has a quirck on this edge case. */
            sdsfree(r->buf);
            r->buf = sdsempty();
        } else {
            r->buf = sdsrange(r->buf,r->pos,sdslen(r->buf));
        }
        r->pos = 0;
    }

    /* Emit a reply when there is one. */
    if (r->rpos == r->rlen) {
        void *reply = r->reply;
        assert(reply != NULL);
        r->reply = NULL;

        /* Destroy the buffer when it is empty and is quite large. */
        if (sdslen(r->buf) == 0 && sdsavail(r->buf) > 16*1024) {
            sdsfree(r->buf);
            r->buf = sdsempty();
            r->pos = 0;
        }

        /* Set list of items to read to be empty. */
        r->rlen = r->rpos = 0;
        return reply;
    } else {
        return NULL;
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
