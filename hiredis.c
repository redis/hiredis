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
#include <errno.h>

#include "hiredis.h"
#include "anet.h"
#include "sds.h"

typedef struct redisReader {
    struct redisReplyObjectFunctions *fn;
    sds error; /* holds optional error */
    void *reply; /* holds temporary reply */

    sds buf; /* read buffer */
    unsigned int pos; /* buffer cursor */

    redisReadTask *rlist; /* list of items to process */
    unsigned int rlen; /* list length */
    unsigned int rpos; /* list cursor */
} redisReader;

static redisReply *createReplyObject(int type);
static void *createStringObject(redisReadTask *task, char *str, size_t len);
static void *createArrayObject(redisReadTask *task, int elements);
static void *createIntegerObject(redisReadTask *task, long long value);
static void *createNilObject(redisReadTask *task);
static void redisSetReplyReaderError(redisReader *r, sds err);

/* Default set of functions to build the reply. */
static redisReplyObjectFunctions defaultFunctions = {
    createStringObject,
    createArrayObject,
    createIntegerObject,
    createNilObject,
    freeReplyObject
};

/* We simply abort on out of memory */
static void redisOOM(void) {
    fprintf(stderr,"Out of memory in hiredis.c");
    exit(1);
}

/* Create a reply object */
static redisReply *createReplyObject(int type) {
    redisReply *r = calloc(sizeof(*r),1);

    if (!r) redisOOM();
    r->type = type;
    return r;
}

/* Free a reply object */
void freeReplyObject(void *reply) {
    redisReply *r = reply;
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
        if (r->str != NULL)
            free(r->str);
        break;
    }
    free(r);
}

static void *createStringObject(redisReadTask *task, char *str, size_t len) {
    redisReply *r = createReplyObject(task->type);
    char *value = malloc(len+1);
    if (!value) redisOOM();
    assert(task->type == REDIS_REPLY_ERROR ||
           task->type == REDIS_REPLY_STATUS ||
           task->type == REDIS_REPLY_STRING);

    /* Copy string value */
    memcpy(value,str,len);
    value[len] = '\0';
    r->str = value;
    r->len = len;

    if (task->parent) {
        redisReply *parent = task->parent;
        assert(parent->type == REDIS_REPLY_ARRAY);
        parent->element[task->idx] = r;
    }
    return r;
}

static void *createArrayObject(redisReadTask *task, int elements) {
    redisReply *r = createReplyObject(REDIS_REPLY_ARRAY);
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
    redisReply *r = createReplyObject(REDIS_REPLY_INTEGER);
    r->integer = value;
    if (task->parent) {
        redisReply *parent = task->parent;
        assert(parent->type == REDIS_REPLY_ARRAY);
        parent->element[task->idx] = r;
    }
    return r;
}

static void *createNilObject(redisReadTask *task) {
    redisReply *r = createReplyObject(REDIS_REPLY_NIL);
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
            obj = r->fn->createInteger(cur,strtoll(p,NULL,10));
        } else {
            obj = r->fn->createString(cur,p,len);
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
            obj = r->fn->createNil(cur);
        } else {
            /* Only continue when the buffer contains the entire bulk item. */
            bytelen += len+2; /* include \r\n */
            if (r->pos+bytelen <= sdslen(r->buf)) {
                obj = r->fn->createString(cur,s+2,len);
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
            obj = r->fn->createNil(cur);
        } else {
            obj = r->fn->createArray(cur,elements);

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
                redisSetReplyReaderError(r,sdscatprintf(sdsempty(),
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
        redisSetReplyReaderError(r,sdscatprintf(sdsempty(),
            "unknown item type '%d'", cur->type));
        return -1;
    }
}

void *redisReplyReaderCreate(redisReplyObjectFunctions *fn) {
    redisReader *r = calloc(sizeof(redisReader),1);
    r->error = NULL;
    r->fn = fn == NULL ? &defaultFunctions : fn;
    r->buf = sdsempty();
    r->rlist = malloc(sizeof(redisReadTask)*1);
    return r;
}

/* External libraries wrapping hiredis might need access to the temporary
 * variable while the reply is built up. When the reader contains an
 * object in between receiving some bytes to parse, this object might
 * otherwise be free'd by garbage collection. */
void *redisReplyReaderGetObject(void *reader) {
    redisReader *r = reader;
    return r->reply;
}

void redisReplyReaderFree(void *reader) {
    redisReader *r = reader;
    if (r->error != NULL)
        sdsfree(r->error);
    if (r->reply != NULL)
        r->fn->freeObject(r->reply);
    if (r->buf != NULL)
        sdsfree(r->buf);
    if (r->rlist != NULL)
        free(r->rlist);
    free(r);
}

static void redisSetReplyReaderError(redisReader *r, sds err) {
    if (r->reply != NULL)
        r->fn->freeObject(r->reply);

    /* Clear remaining buffer when we see a protocol error. */
    if (r->buf != NULL) {
        sdsfree(r->buf);
        r->buf = sdsempty();
        r->pos = 0;
    }
    r->rlen = r->rpos = 0;
    r->error = err;
}

char *redisReplyReaderGetError(void *reader) {
    redisReader *r = reader;
    return r->error;
}

void redisReplyReaderFeed(void *reader, char *buf, int len) {
    redisReader *r = reader;

    /* Copy the provided buffer. */
    if (buf != NULL && len >= 1)
        r->buf = sdscatlen(r->buf,buf,len);
}

int redisReplyReaderGetReply(void *reader, void **reply) {
    redisReader *r = reader;
    if (reply != NULL) *reply = NULL;

    /* When the buffer is empty, there will never be a reply. */
    if (sdslen(r->buf) == 0)
        return REDIS_OK;

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
        void *aux = r->reply;
        r->reply = NULL;

        /* Destroy the buffer when it is empty and is quite large. */
        if (sdslen(r->buf) == 0 && sdsavail(r->buf) > 16*1024) {
            sdsfree(r->buf);
            r->buf = sdsempty();
            r->pos = 0;
        }

        /* Set list of items to read to be empty. */
        r->rlen = r->rpos = 0;

        /* Check if there actually *is* a reply. */
        if (r->error != NULL) {
            return REDIS_ERR;
        } else {
            if (reply != NULL) *reply = aux;
        }
    }
    return REDIS_OK;
}

/* Calculate the number of bytes needed to represent an integer as string. */
static int intlen(int i) {
    int len = 0;
    if (i < 0) {
        len++;
        i = -i;
    }
    do {
        len++;
        i /= 10;
    } while(i);
    return len;
}

/* Helper function for redisvFormatCommand(). */
static void addArgument(sds a, char ***argv, int *argc, int *totlen) {
    (*argc)++;
    if ((*argv = realloc(*argv, sizeof(char*)*(*argc))) == NULL) redisOOM();
    if (totlen) *totlen = *totlen+1+intlen(sdslen(a))+2+sdslen(a)+2;
    (*argv)[(*argc)-1] = a;
}

int redisvFormatCommand(char **target, const char *format, va_list ap) {
    size_t size;
    const char *arg, *c = format;
    char *cmd = NULL; /* final command */
    int pos; /* position in final command */
    sds current; /* current argument */
    char **argv = NULL;
    int argc = 0, j;
    int totlen = 0;

    /* Abort if there is not target to set */
    if (target == NULL)
        return -1;

    /* Build the command string accordingly to protocol */
    current = sdsempty();
    while(*c != '\0') {
        if (*c != '%' || c[1] == '\0') {
            if (*c == ' ') {
                if (sdslen(current) != 0) {
                    addArgument(current, &argv, &argc, &totlen);
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

    /* Add the last argument if needed */
    if (sdslen(current) != 0) {
        addArgument(current, &argv, &argc, &totlen);
    } else {
        sdsfree(current);
    }

    /* Add bytes needed to hold multi bulk count */
    totlen += 1+intlen(argc)+2;

    /* Build the command at protocol level */
    cmd = malloc(totlen+1);
    if (!cmd) redisOOM();
    pos = sprintf(cmd,"*%d\r\n",argc);
    for (j = 0; j < argc; j++) {
        pos += sprintf(cmd+pos,"$%zu\r\n",sdslen(argv[j]));
        memcpy(cmd+pos,argv[j],sdslen(argv[j]));
        pos += sdslen(argv[j]);
        sdsfree(argv[j]);
        cmd[pos++] = '\r';
        cmd[pos++] = '\n';
    }
    assert(pos == totlen);
    free(argv);
    cmd[totlen] = '\0';
    *target = cmd;
    return totlen;
}

/* Format a command according to the Redis protocol. This function
 * takes a format similar to printf:
 *
 * %s represents a C null terminated string you want to interpolate
 * %b represents a binary safe string
 *
 * When using %b you need to provide both the pointer to the string
 * and the length in bytes. Examples:
 *
 * len = redisFormatCommand(target, "GET %s", mykey);
 * len = redisFormatCommand(target, "SET %s %b", mykey, myval, myvallen);
 */
int redisFormatCommand(char **target, const char *format, ...) {
    va_list ap;
    int len;
    va_start(ap,format);
    len = redisvFormatCommand(target,format,ap);
    va_end(ap);
    return len;
}

/* Format a command according to the Redis protocol. This function takes the
 * number of arguments, an array with arguments and an array with their
 * lengths. If the latter is set to NULL, strlen will be used to compute the
 * argument lengths.
 */
int redisFormatCommandArgv(char **target, int argc, const char **argv, const size_t *argvlen) {
    char *cmd = NULL; /* final command */
    int pos; /* position in final command */
    size_t len;
    int totlen, j;

    /* Calculate number of bytes needed for the command */
    totlen = 1+intlen(argc)+2;
    for (j = 0; j < argc; j++) {
        len = argvlen ? argvlen[j] : strlen(argv[j]);
        totlen += 1+intlen(len)+2+len+2;
    }

    /* Build the command at protocol level */
    cmd = malloc(totlen+1);
    if (!cmd) redisOOM();
    pos = sprintf(cmd,"*%d\r\n",argc);
    for (j = 0; j < argc; j++) {
        len = argvlen ? argvlen[j] : strlen(argv[j]);
        pos += sprintf(cmd+pos,"$%zu\r\n",len);
        memcpy(cmd+pos,argv[j],len);
        pos += len;
        cmd[pos++] = '\r';
        cmd[pos++] = '\n';
    }
    assert(pos == totlen);
    cmd[totlen] = '\0';
    *target = cmd;
    return totlen;
}

static int redisContextConnect(redisContext *c, const char *ip, int port) {
    char err[ANET_ERR_LEN];
    if (c->flags & REDIS_BLOCK) {
        c->fd = anetTcpConnect(err,(char*)ip,port);
    } else {
        c->fd = anetTcpNonBlockConnect(err,(char*)ip,port);
    }

    if (c->fd == ANET_ERR) {
        c->error = sdsnew(err);
        return REDIS_ERR;
    }
    if (anetTcpNoDelay(err,c->fd) == ANET_ERR) {
        c->error = sdsnew(err);
        return REDIS_ERR;
    }
    return REDIS_OK;
}

static redisContext *redisContextInit(redisReplyObjectFunctions *fn) {
    redisContext *c = calloc(sizeof(redisContext),1);
    c->error = NULL;
    c->obuf = sdsempty();
    c->fn = fn == NULL ? &defaultFunctions : fn;
    c->reader = redisReplyReaderCreate(c->fn);
    return c;
}

void redisDisconnect(redisContext *c) {
    if (c->cbDisconnect.fn != NULL)
        c->cbDisconnect.fn(c,c->cbDisconnect.privdata);
    close(c->fd);
    c->flags &= ~REDIS_CONNECTED;
}

void redisFree(redisContext *c) {
    /* Disconnect before free'ing if not yet disconnected. */
    if (c->flags & REDIS_CONNECTED)
        redisDisconnect(c);

    /* Fire free callback and clear all allocations. */
    if (c->cbFree.fn != NULL)
        c->cbFree.fn(c,c->cbFree.privdata);
    if (c->error != NULL)
        sdsfree(c->error);
    if (c->obuf != NULL)
        sdsfree(c->obuf);
    redisReplyReaderFree(c->reader);
    free(c);
}

/* Connect to a Redis instance. On error the field error in the returned
 * context will be set to the return value of the error function.
 * When no set of reply functions is given, the default set will be used. */
redisContext *redisConnect(const char *ip, int port, redisReplyObjectFunctions *fn) {
    redisContext *c = redisContextInit(fn);
    c->flags |= REDIS_BLOCK;
    c->flags |= REDIS_CONNECTED;
    redisContextConnect(c,ip,port);
    return c;
}

redisContext *redisConnectNonBlock(const char *ip, int port, redisReplyObjectFunctions *fn) {
    redisContext *c = redisContextInit(fn);
    c->flags &= ~REDIS_BLOCK;
    c->flags |= REDIS_CONNECTED;
    redisContextConnect(c,ip,port);
    return c;
}

/* Register callback that is triggered when redisDisconnect is called. */
void redisSetDisconnectCallback(redisContext *c, redisContextCallbackFn *fn, void *privdata) {
    c->cbDisconnect.fn = fn;
    c->cbDisconnect.privdata = privdata;
}

/* Register callback that is triggered when a command is put in the output
 * buffer when the context is non-blocking. */
void redisSetCommandCallback(redisContext *c, redisContextCallbackFn *fn, void *privdata) {
    c->cbCommand.fn = fn;
    c->cbCommand.privdata = privdata;
}

/* Register callback that is triggered when the context is free'd. */
void redisSetFreeCallback(redisContext *c, redisContextCallbackFn *fn, void *privdata) {
    c->cbFree.fn = fn;
    c->cbFree.privdata = privdata;
}

/* Use this function to handle a read event on the descriptor. It will try
 * and read some bytes from the socket and feed them to the reply parser.
 *
 * After this function is called, you may use redisContextReadReply to
 * see if there is a reply available. */
int redisBufferRead(redisContext *c) {
    char buf[2048];
    int nread = read(c->fd,buf,sizeof(buf));
    if (nread == -1) {
        if (errno == EAGAIN) {
            /* Try again later */
        } else {
            /* Set error in context */
            c->error = sdscatprintf(sdsempty(),
                "read: %s", strerror(errno));
            return REDIS_ERR;
        }
    } else if (nread == 0) {
        c->error = sdscatprintf(sdsempty(),
            "read: Server closed the connection");
        return REDIS_ERR;
    } else {
        redisReplyReaderFeed(c->reader,buf,nread);
    }
    return REDIS_OK;
}

int redisGetReply(redisContext *c, void **reply) {
    if (redisReplyReaderGetReply(c->reader,reply) == REDIS_ERR) {
        /* Copy the (protocol) error from the reader to the context. */
        c->error = sdsnew(((redisReader*)c->reader)->error);
        return REDIS_ERR;
    }
    return REDIS_OK;
}

/* Write the output buffer to the socket.
 *
 * Returns REDIS_OK when the buffer is empty, or (a part of) the buffer was
 * succesfully written to the socket. When the buffer is empty after the
 * write operation, "wdone" is set to 1 (if given).
 *
 * Returns REDIS_ERR if an error occured trying to write and sets
 * c->error to hold the appropriate error string.
 */
int redisBufferWrite(redisContext *c, int *done) {
    int nwritten;
    if (sdslen(c->obuf) > 0) {
        nwritten = write(c->fd,c->obuf,sdslen(c->obuf));
        if (nwritten == -1) {
            if (errno == EAGAIN) {
                /* Try again later */
            } else {
                /* Set error in context */
                c->error = sdscatprintf(sdsempty(),
                    "write: %s", strerror(errno));
                return REDIS_ERR;
            }
        } else if (nwritten > 0) {
            if (nwritten == (signed)sdslen(c->obuf)) {
                sdsfree(c->obuf);
                c->obuf = sdsempty();
            } else {
                c->obuf = sdsrange(c->obuf,nwritten,-1);
            }
        }
    }
    if (done != NULL) *done = (sdslen(c->obuf) == 0);
    return REDIS_OK;
}

static int redisCommandWriteBlock(redisContext *c, void **reply, char *str, size_t len) {
    int wdone = 0;
    void *aux = NULL;
    assert(c->flags & REDIS_BLOCK);
    c->obuf = sdscatlen(c->obuf,str,len);

    /* Write until done. */
    do {
        if (redisBufferWrite(c,&wdone) == REDIS_ERR)
            return REDIS_ERR;
    } while (!wdone);

    /* Read until there is a reply. */
    do {
        if (redisBufferRead(c) == REDIS_ERR)
            return REDIS_ERR;
        if (redisGetReply(c,&aux) == REDIS_ERR)
            return REDIS_ERR;
    } while (aux == NULL);

    /* Set reply object. */
    if (reply != NULL)
        *reply = aux;

    return REDIS_OK;
}

static int redisCommandWriteNonBlock(redisContext *c, char *str, size_t len) {
    assert(!(c->flags & REDIS_BLOCK));
    c->obuf = sdscatlen(c->obuf,str,len);

    /* Fire write callback */
    if (c->cbCommand.fn != NULL)
        c->cbCommand.fn(c,c->cbCommand.privdata);

    return REDIS_OK;
}

/* Write a formatted command to the output buffer. If the given context is
 * blocking, immediately read the reply into the "reply" pointer. When the
 * context is non-blocking, the "reply" pointer will not be used and the
 * command is simply appended to the write buffer.
 *
 * Returns the reply when a reply was succesfully retrieved. Returns NULL
 * otherwise. When NULL is returned in a blocking context, the error field
 * in the context will be set. */
void *redisCommand(redisContext *c, const char *format, ...) {
    va_list ap;
    char *cmd;
    int len;
    void *reply = NULL;
    va_start(ap,format);
    len = redisvFormatCommand(&cmd,format,ap);
    va_end(ap);

    if (c->flags & REDIS_BLOCK) {
        if (redisCommandWriteBlock(c,&reply,cmd,len) == REDIS_OK) {
            free(cmd);
            return reply;
        }
    } else {
        redisCommandWriteNonBlock(c,cmd,len);
    }
    free(cmd);
    return NULL;
}
