/*
 * Copyright (c) 2009-2011, Salvatore Sanfilippo <antirez at gmail dot com>
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

#include "fmacros.h"
#include "alloc.h"
#include <stdlib.h>
#include <string.h>
#ifndef _MSC_VER
#include <strings.h>
#endif
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include "async.h"
#include "net.h"
#include "dict.c"
#include "sds.h"
#include "win32.h"

#include "async_private.h"

#ifdef NDEBUG
#undef assert
#define assert(e) (void)(e)
#endif

/* Internal macros used by checkPubsubReply(). They have one bit each, although
 * only some combinations are used. */
#define PUBSUB_REPLY_MESSAGE 1
#define PUBSUB_REPLY_SUBSCRIBE 2
#define PUBSUB_REPLY_UNSUBSCRIBE 4
#define PUBSUB_REPLY_REGULAR 8
#define PUBSUB_REPLY_PATTERN 16
#define PUBSUB_REPLY_SHARDED 32

/* Special negative values for a callback's `pending_replies` fields. */
#define PENDING_REPLY_UNSUBSCRIBE_ALL -1
#define PENDING_REPLY_MONITOR -2
#define PENDING_REPLY_RESET -3

/* Forward declarations of hiredis.c functions */
int __redisAppendCommand(redisContext *c, const char *cmd, size_t len);
void __redisSetError(redisContext *c, int type, const char *str);

/* Reference counting for callback struct. */
static void callbackIncrRefCount(redisAsyncContext *ac, redisCallback *cb) {
    (void)ac;
    cb->refcount++;
}
static void callbackDecrRefCount(redisAsyncContext *ac, redisCallback *cb) {
    cb->refcount--;
    if (cb->refcount == 0) {
        if (cb->finalizer != NULL) {
            redisContext *c = &(ac->c);
            c->flags |= REDIS_IN_CALLBACK;
            cb->finalizer(ac, cb->privdata);
            c->flags &= ~REDIS_IN_CALLBACK;
        }
        hi_free(cb);
    }
}

/* Functions managing dictionary of callbacks for pub/sub. */
static unsigned int callbackHash(const void *key) {
    return dictGenHashFunction((const unsigned char *)key,
                               sdslen((const sds)key));
}

static int callbackKeyCompare(void *privdata, const void *key1, const void *key2) {
    int l1, l2;
    ((void) privdata);

    l1 = sdslen((const sds)key1);
    l2 = sdslen((const sds)key2);
    if (l1 != l2) return 0;
    return memcmp(key1,key2,l1) == 0;
}

static void callbackKeyDestructor(void *privdata, void *key) {
    ((void) privdata);
    sdsfree((sds)key);
}

static void *callbackValDup(void *privdata, const void *val) {
    callbackIncrRefCount((redisAsyncContext *)privdata, (redisCallback *)val);
    return (void *)val;
}

static void callbackValDestructor(void *privdata, void *val) {
    callbackDecrRefCount((redisAsyncContext *)privdata, (redisCallback *)val);
}

static dictType callbackDict = {
    callbackHash,
    NULL, /* key dup */
    callbackValDup,
    callbackKeyCompare,
    callbackKeyDestructor,
    callbackValDestructor
};

static redisAsyncContext *redisAsyncInitialize(redisContext *c) {
    redisAsyncContext *ac;
    dict *channels = NULL, *patterns = NULL, *shard_channels = NULL;

    ac = hi_realloc(c,sizeof(redisAsyncContext));
    if (ac == NULL)
        goto oom;

    channels = dictCreate(&callbackDict, ac);
    if (channels == NULL)
        goto oom;

    patterns = dictCreate(&callbackDict, ac);
    if (patterns == NULL)
        goto oom;

    shard_channels = dictCreate(&callbackDict, ac);
    if (shard_channels == NULL)
        goto oom;

    c = &(ac->c);

    /* The regular connect functions will always set the flag REDIS_CONNECTED.
     * For the async API, we want to wait until the first write event is
     * received up before setting this flag, so reset it here. */
    c->flags &= ~REDIS_CONNECTED;

    ac->err = 0;
    ac->errstr = NULL;
    ac->data = NULL;
    ac->dataCleanup = NULL;

    ac->ev.data = NULL;
    ac->ev.addRead = NULL;
    ac->ev.delRead = NULL;
    ac->ev.addWrite = NULL;
    ac->ev.delWrite = NULL;
    ac->ev.cleanup = NULL;
    ac->ev.scheduleTimer = NULL;

    ac->onConnect = NULL;
    ac->onConnectNC = NULL;
    ac->onDisconnect = NULL;

    ac->replies.head = NULL;
    ac->replies.tail = NULL;
    ac->sub.channels = channels;
    ac->sub.patterns = patterns;
    ac->sub.shard_channels = shard_channels;
    ac->sub.pending_commands = 0;
    ac->monitor_cb = NULL;

    return ac;
oom:
    if (channels) dictRelease(channels);
    if (patterns) dictRelease(patterns);
    if (shard_channels) dictRelease(shard_channels);
    if (ac) hi_free(ac);
    return NULL;
}

/* We want the error field to be accessible directly instead of requiring
 * an indirection to the redisContext struct. */
static void __redisAsyncCopyError(redisAsyncContext *ac) {
    if (!ac)
        return;

    redisContext *c = &(ac->c);
    ac->err = c->err;
    ac->errstr = c->errstr;
}

redisAsyncContext *redisAsyncConnectWithOptions(const redisOptions *options) {
    redisOptions myOptions = *options;
    redisContext *c;
    redisAsyncContext *ac;

    /* Clear any erroneously set sync callback and flag that we don't want to
     * use freeReplyObject by default. */
    myOptions.push_cb = NULL;
    myOptions.options |= REDIS_OPT_NO_PUSH_AUTOFREE;

    myOptions.options |= REDIS_OPT_NONBLOCK;
    c = redisConnectWithOptions(&myOptions);
    if (c == NULL) {
        return NULL;
    }

    ac = redisAsyncInitialize(c);
    if (ac == NULL) {
        redisFree(c);
        return NULL;
    }

    /* Set any configured async push handler */
    redisAsyncSetPushCallback(ac, myOptions.async_push_cb);

    __redisAsyncCopyError(ac);
    return ac;
}

redisAsyncContext *redisAsyncConnect(const char *ip, int port) {
    redisOptions options = {0};
    REDIS_OPTIONS_SET_TCP(&options, ip, port);
    return redisAsyncConnectWithOptions(&options);
}

redisAsyncContext *redisAsyncConnectBind(const char *ip, int port,
                                         const char *source_addr) {
    redisOptions options = {0};
    REDIS_OPTIONS_SET_TCP(&options, ip, port);
    options.endpoint.tcp.source_addr = source_addr;
    return redisAsyncConnectWithOptions(&options);
}

redisAsyncContext *redisAsyncConnectBindWithReuse(const char *ip, int port,
                                                  const char *source_addr) {
    redisOptions options = {0};
    REDIS_OPTIONS_SET_TCP(&options, ip, port);
    options.options |= REDIS_OPT_REUSEADDR;
    options.endpoint.tcp.source_addr = source_addr;
    return redisAsyncConnectWithOptions(&options);
}

redisAsyncContext *redisAsyncConnectUnix(const char *path) {
    redisOptions options = {0};
    REDIS_OPTIONS_SET_UNIX(&options, path);
    return redisAsyncConnectWithOptions(&options);
}

static int
redisAsyncSetConnectCallbackImpl(redisAsyncContext *ac, redisConnectCallback *fn,
                                 redisConnectCallbackNC *fn_nc)
{
    /* If either are already set, this is an error */
    if (ac->onConnect || ac->onConnectNC)
        return REDIS_ERR;

    if (fn) {
        ac->onConnect = fn;
    } else if (fn_nc) {
        ac->onConnectNC = fn_nc;
    }

    /* The common way to detect an established connection is to wait for
     * the first write event to be fired. This assumes the related event
     * library functions are already set. */
    _EL_ADD_WRITE(ac);

    return REDIS_OK;
}

int redisAsyncSetConnectCallback(redisAsyncContext *ac, redisConnectCallback *fn) {
    return redisAsyncSetConnectCallbackImpl(ac, fn, NULL);
}

int redisAsyncSetConnectCallbackNC(redisAsyncContext *ac, redisConnectCallbackNC *fn) {
    return redisAsyncSetConnectCallbackImpl(ac, NULL, fn);
}

int redisAsyncSetDisconnectCallback(redisAsyncContext *ac, redisDisconnectCallback *fn) {
    if (ac->onDisconnect == NULL) {
        ac->onDisconnect = fn;
        return REDIS_OK;
    }
    return REDIS_ERR;
}

/* Helper functions to push/shift callbacks */
static int __redisPushCallback(redisCallbackList *list, redisCallback *cb) {
    assert(cb != NULL);
    cb->next = NULL;

    /* Store callback in list */
    if (list->head == NULL)
        list->head = cb;
    if (list->tail != NULL)
        list->tail->next = cb;
    list->tail = cb;
    return REDIS_OK;
}

static int __redisShiftCallback(redisCallbackList *list, redisCallback **target) {
    redisCallback *cb = list->head;
    if (cb != NULL) {
        list->head = cb->next;
        if (cb == list->tail)
            list->tail = NULL;

        if (target != NULL)
            *target = cb;
        return REDIS_OK;
    }
    return REDIS_ERR;
}

static int __redisUnshiftCallback(redisCallbackList *list, redisCallback *cb) {
    assert(cb != NULL);
    cb->next = list->head;
    list->head = cb;
    return REDIS_OK;
}

/* Runs callback and frees the reply (except if REDIS_NO_AUTO_FREE_REPLIES is set) */
static void __redisRunCallback(redisAsyncContext *ac, redisCallback *cb, redisReply *reply) {
    redisContext *c = &(ac->c);
    if (cb->fn != NULL) {
        c->flags |= REDIS_IN_CALLBACK;
        cb->fn(ac,reply,cb->privdata);
        c->flags &= ~REDIS_IN_CALLBACK;
    }
    if (!(c->flags & REDIS_NO_AUTO_FREE_REPLIES) || cb->fn == NULL) {
        if (reply != NULL) c->reader->fn->freeObject(reply);
    }
}

static void __redisRunPushCallback(redisAsyncContext *ac, redisReply *reply) {
    if (ac->push_cb != NULL) {
        ac->c.flags |= REDIS_IN_CALLBACK;
        ac->push_cb(ac, reply);
        ac->c.flags &= ~REDIS_IN_CALLBACK;
    }
}

static void __redisRunConnectCallback(redisAsyncContext *ac, int status)
{
    if (ac->onConnect == NULL && ac->onConnectNC == NULL)
        return;

    if (!(ac->c.flags & REDIS_IN_CALLBACK)) {
        ac->c.flags |= REDIS_IN_CALLBACK;
        if (ac->onConnect) {
            ac->onConnect(ac, status);
        } else {
            ac->onConnectNC(ac, status);
        }
        ac->c.flags &= ~REDIS_IN_CALLBACK;
    } else {
        /* already in callback */
        if (ac->onConnect) {
            ac->onConnect(ac, status);
        } else {
            ac->onConnectNC(ac, status);
        }
    }
}

static void __redisRunDisconnectCallback(redisAsyncContext *ac, int status)
{
    if (ac->onDisconnect) {
        if (!(ac->c.flags & REDIS_IN_CALLBACK)) {
            ac->c.flags |= REDIS_IN_CALLBACK;
            ac->onDisconnect(ac, status);
            ac->c.flags &= ~REDIS_IN_CALLBACK;
        } else {
            /* already in callback */
            ac->onDisconnect(ac, status);
        }
    }
}

/* Helper function to free the context. */
static void __redisAsyncFree(redisAsyncContext *ac) {
    redisContext *c = &(ac->c);
    redisCallback *cb;
    dictIterator it;
    dictEntry *de;

    /* Execute pending callbacks with NULL reply. */
    while (__redisShiftCallback(&ac->replies,&cb) == REDIS_OK) {
        __redisRunCallback(ac,cb,NULL);
        callbackDecrRefCount(ac, cb);
    }

    /* Run subscription callbacks with NULL reply */
    if (ac->sub.channels) {
        dictInitIterator(&it,ac->sub.channels);
        while ((de = dictNext(&it)) != NULL)
            __redisRunCallback(ac,dictGetEntryVal(de),NULL);

        dictRelease(ac->sub.channels);
    }

    if (ac->sub.patterns) {
        dictInitIterator(&it,ac->sub.patterns);
        while ((de = dictNext(&it)) != NULL)
            __redisRunCallback(ac,dictGetEntryVal(de),NULL);

        dictRelease(ac->sub.patterns);
    }

    if (ac->sub.shard_channels) {
        dictInitIterator(&it,ac->sub.shard_channels);
        while ((de = dictNext(&it)) != NULL)
            __redisRunCallback(ac,dictGetEntryVal(de),NULL);

        dictRelease(ac->sub.shard_channels);
    }

    if (ac->monitor_cb != NULL) {
        callbackDecrRefCount(ac, ac->monitor_cb);
    }

    /* Signal event lib to clean up */
    _EL_CLEANUP(ac);

    /* Execute disconnect callback. When redisAsyncFree() initiated destroying
     * this context, the status will always be REDIS_OK. */
    if (c->flags & REDIS_CONNECTED) {
        int status = ac->err == 0 ? REDIS_OK : REDIS_ERR;
        if (c->flags & REDIS_FREEING)
            status = REDIS_OK;
        __redisRunDisconnectCallback(ac, status);
    }

    if (ac->dataCleanup) {
        ac->dataCleanup(ac->data);
    }

    /* Cleanup self */
    redisFree(c);
}

/* Free the async context. When this function is called from a callback,
 * control needs to be returned to redisProcessCallbacks() before actual
 * free'ing. To do so, a flag is set on the context which is picked up by
 * redisProcessCallbacks(). Otherwise, the context is immediately free'd. */
void redisAsyncFree(redisAsyncContext *ac) {
    if (ac == NULL)
        return;

    redisContext *c = &(ac->c);

    c->flags |= REDIS_FREEING;
    if (!(c->flags & REDIS_IN_CALLBACK))
        __redisAsyncFree(ac);
}

/* Helper function to make the disconnect happen and clean up. */
void __redisAsyncDisconnect(redisAsyncContext *ac) {
    redisContext *c = &(ac->c);

    /* Make sure error is accessible if there is any */
    __redisAsyncCopyError(ac);

    if (ac->err == 0) {
        /* For clean disconnects, there should be no pending callbacks. */
        int ret = __redisShiftCallback(&ac->replies,NULL);
        assert(ret == REDIS_ERR);
    } else {
        /* Disconnection is caused by an error, make sure that pending
         * callbacks cannot call new commands. */
        c->flags |= REDIS_DISCONNECTING;
    }

    /* cleanup event library on disconnect.
     * this is safe to call multiple times */
    _EL_CLEANUP(ac);

    /* For non-clean disconnects, __redisAsyncFree() will execute pending
     * callbacks with a NULL-reply. */
    if (!(c->flags & REDIS_NO_AUTO_FREE)) {
      __redisAsyncFree(ac);
    }
}

/* Tries to do a clean disconnect from Redis, meaning it stops new commands
 * from being issued, but tries to flush the output buffer and execute
 * callbacks for all remaining replies. When this function is called from a
 * callback, there might be more replies and we can safely defer disconnecting
 * to redisProcessCallbacks(). Otherwise, we can only disconnect immediately
 * when there are no pending callbacks. */
void redisAsyncDisconnect(redisAsyncContext *ac) {
    redisContext *c = &(ac->c);
    c->flags |= REDIS_DISCONNECTING;

    /** unset the auto-free flag here, because disconnect undoes this */
    c->flags &= ~REDIS_NO_AUTO_FREE;
    if (!(c->flags & REDIS_IN_CALLBACK) && ac->replies.head == NULL)
        __redisAsyncDisconnect(ac);
}

/* Returns a bitwise or of the PUBSUB_REPLY_ macros. 0 means not pubsub. */
static int checkPubsubReply(redisReply *reply, int expect_push) {

    /* Match reply with the expected format of a pushed message.
     * The type and number of elements (3 to 4) are specified at:
     * https://redis.io/topics/pubsub#format-of-pushed-messages */
    if (reply->type != (expect_push ? REDIS_REPLY_PUSH : REDIS_REPLY_ARRAY) ||
        reply->elements < 3 ||
        reply->elements > 4 ||
        reply->element[0]->type != REDIS_REPLY_STRING ||
        reply->element[0]->len < sizeof("message") - 1)
    {
        return 0;
    }

    char *str = reply->element[0]->str;
    size_t len = reply->element[0]->len;

    if (!strncmp(str, "message", len))
        return PUBSUB_REPLY_MESSAGE | PUBSUB_REPLY_REGULAR;
    if (!strncmp(str, "subscribe", len))
        return PUBSUB_REPLY_SUBSCRIBE | PUBSUB_REPLY_REGULAR;
    if (!strncmp(str, "unsubscribe", len))
        return PUBSUB_REPLY_UNSUBSCRIBE | PUBSUB_REPLY_REGULAR;

    if (!strncmp(str, "pmessage", len))
        return PUBSUB_REPLY_MESSAGE | PUBSUB_REPLY_PATTERN;
    if (!strncmp(str, "psubscribe", len))
        return PUBSUB_REPLY_SUBSCRIBE | PUBSUB_REPLY_PATTERN;
    if (!strncmp(str, "punsubscribe", len))
        return PUBSUB_REPLY_UNSUBSCRIBE | PUBSUB_REPLY_PATTERN;

    if (!strncmp(str, "smessage", len))
        return PUBSUB_REPLY_MESSAGE | PUBSUB_REPLY_SHARDED;
    if (!strncmp(str, "ssubscribe", len))
        return PUBSUB_REPLY_SUBSCRIBE | PUBSUB_REPLY_SHARDED;
    if (!strncmp(str, "sunsubscribe", len))
        return PUBSUB_REPLY_UNSUBSCRIBE | PUBSUB_REPLY_SHARDED;

    return 0;
}

/* Returns the dict used for callbacks per channel/pattern/shard-channel. */
static dict *getPubsubCallbackDict(redisAsyncContext *ac, int pubsub_flags) {
    if (pubsub_flags & PUBSUB_REPLY_REGULAR) return ac->sub.channels;
    if (pubsub_flags & PUBSUB_REPLY_PATTERN) return ac->sub.patterns;
    if (pubsub_flags & PUBSUB_REPLY_SHARDED) return ac->sub.shard_channels;
    return NULL;
}

/* Handles a pubsub reply, delegates the reply to the right callback and frees
 * the reply. The passed callback `cb` should be the one queued when the
 * corresponding command was sent ('subscribe' and 'unsubscribe') and NULL for
 * 'message'. */
static int handlePubsubReply(redisAsyncContext *ac, redisReply *reply,
                             int pubsub_flags, redisCallback *cb) {
    dict *callbacks = getPubsubCallbackDict(ac, pubsub_flags);
    sds sname = sdsnewlen(reply->element[1]->str,reply->element[1]->len);
    if (sname == NULL) goto oom;
    dictEntry *de = dictFind(callbacks, sname);
    redisCallback *existcb = (de != NULL) ? dictGetEntryVal(de) : NULL;

    if (pubsub_flags & PUBSUB_REPLY_MESSAGE) {
        sdsfree(sname);
        __redisRunCallback(ac, existcb, reply);
        return REDIS_OK;
    }

    /* Subscribe and unsubscribe */
    if (pubsub_flags & PUBSUB_REPLY_SUBSCRIBE) {
        /* Add channel subscription and call the callback */
        if (existcb != NULL && cb->fn == NULL) {
            /* Don't replace the existing callback */
            sdsfree(sname);
            __redisRunCallback(ac, existcb, reply);
        } else {
            /* Set or replace callback */
            int ret = dictReplace(callbacks, sname, cb);
            if (ret == 0) sdsfree(sname);
            __redisRunCallback(ac, cb, reply);
        }
    } else if (pubsub_flags & PUBSUB_REPLY_UNSUBSCRIBE) {
        /* If we've unsubscribed to the last channel, the command is done. */
        /* Check if this was the last channel unsubscribed. */
        assert(reply->element[2]->type == REDIS_REPLY_INTEGER);
        if (cb->pending_replies == PENDING_REPLY_UNSUBSCRIBE_ALL &&
            reply->element[2]->integer == 0)
        {
            cb->pending_replies = 0;
        }

        if (existcb != NULL) {
            /* Invoke the callback used when subscribing. */
            __redisRunCallback(ac, existcb, reply);
        } else {
            /* We were not subscribed to this channel. We could invoke the
             * callback `cb` if passed with the [P|S]UNSUBSCRIBE command here,
             * but legacy is to just free the reply. */
            ac->c.reader->fn->freeObject(reply);
        }

        /* Delete channel subscription. */
        dictDelete(callbacks,sname);
        sdsfree(sname);
    }

    if (cb->pending_replies > 0) cb->pending_replies--;
    if (cb->pending_replies == 0) ac->sub.pending_commands--;

    /* Unset subscribed flag only when not subscribed to any channel and no
     * pipelined pending subscribe or pending unsubscribe replies. */
    if (ac->sub.pending_commands == 0
        && dictSize(ac->sub.channels) == 0
        && dictSize(ac->sub.patterns) == 0
        && dictSize(ac->sub.shard_channels) == 0) {
        ac->c.flags &= ~REDIS_SUBSCRIBED;
    }
    return REDIS_OK;
oom:
    __redisSetError(&(ac->c), REDIS_ERR_OOM, "Out of memory");
    __redisAsyncCopyError(ac);
    return REDIS_ERR;
}

/* Handle the effects of the RESET command. */
static void handleReset(redisAsyncContext *ac) {
    /* Cancel monitoring mode */
    ac->c.flags &= ~REDIS_MONITORING;
    if (ac->monitor_cb != NULL) {
        callbackDecrRefCount(ac, ac->monitor_cb);
        ac->monitor_cb = NULL;
    }

    /* Cancel subscriptions (finalizers are called if any) */
    ac->c.flags &= ~REDIS_SUBSCRIBED;
    if (ac->sub.channels) dictEmpty(ac->sub.channels);
    if (ac->sub.patterns) dictEmpty(ac->sub.patterns);
    if (ac->sub.shard_channels) dictEmpty(ac->sub.shard_channels);
}

void redisProcessCallbacks(redisAsyncContext *ac) {
    redisContext *c = &(ac->c);
    redisReply *reply = NULL;
    int status;

    while((status = redisGetReply(c, (void**)&reply)) == REDIS_OK) {
        if (reply == NULL) {
            /* When the connection is being disconnected and there are
             * no more replies, this is the cue to really disconnect. */
            if (c->flags & REDIS_DISCONNECTING && sdslen(c->obuf) == 0
                && ac->replies.head == NULL) {
                __redisAsyncDisconnect(ac);
                return;
            }
            /* When the connection is not being disconnected, simply stop
             * trying to get replies and wait for the next loop tick. */
            break;
        }

        /* Keep track of push message support for subscribe handling */
        if (redisIsPushReply(reply)) c->flags |= REDIS_SUPPORTS_PUSH;

        /* Categorize pubsub reply if we're in subscribed mode. */
        int pubsub_reply_flags = 0;
        if (c->flags & REDIS_SUBSCRIBED) {
            pubsub_reply_flags = checkPubsubReply(reply, c->flags & REDIS_SUPPORTS_PUSH);
        }

        /* Send any non-subscribe related PUSH messages to our PUSH handler
         * while allowing subscribe related PUSH messages to pass through.
         * This allows existing code to be backward compatible and work in
         * either RESP2 or RESP3 mode. */
        if (redisIsPushReply(reply) && !pubsub_reply_flags) {
            __redisRunPushCallback(ac, reply);
            c->reader->fn->freeObject(reply);
            continue;
        }

        /* Send monitored command to monitor callback */
        if ((c->flags & REDIS_MONITORING) &&
            reply->type == REDIS_REPLY_STATUS && reply->len > 0 &&
            reply->str[0] >= '0' && reply->str[0] <= '9')
        {
            __redisRunCallback(ac, ac->monitor_cb, reply);
            continue;
        }

        /* Get callback from queue which was added when the command was sent. */
        redisCallback *cb = NULL;
        if (pubsub_reply_flags & PUBSUB_REPLY_MESSAGE) {
            /* Pubsub message is the only true out-of-band pubsub reply. There
             * is no callback in the queue. (Subscribe and unsubscribe are
             * actually in-band replies to their corresponding commands even
             * though they are of push type.) */
        } else if (__redisShiftCallback(&ac->replies, &cb) != REDIS_OK) {
            /*
             * A spontaneous reply in a not-subscribed context can be the error
             * reply that is sent when a new connection exceeds the maximum
             * number of allowed connections on the server side.
             *
             * This is seen as an error instead of a regular reply because the
             * server closes the connection after sending it.
             *
             * To prevent the error from being overwritten by an EOF error the
             * connection is closed here. See issue #43.
             *
             * Another possibility is that the server is loading its dataset.
             * In this case we also want to close the connection, and have the
             * user wait until the server is ready to take our request.
             */
            assert(((redisReply*)reply)->type == REDIS_REPLY_ERROR);
            c->err = REDIS_ERR_OTHER;
            snprintf(c->errstr,sizeof(c->errstr),"%s",((redisReply*)reply)->str);
            c->reader->fn->freeObject(reply);
            __redisAsyncDisconnect(ac);
            return;
        }

        if (pubsub_reply_flags != 0) {
            handlePubsubReply(ac, reply, pubsub_reply_flags, cb);
        } else {
            /* Regular reply. This includes ERR reply for subscribe commands. */

            /* Handle special effects of the command's reply, if any. */
            if (cb->pending_replies == PENDING_REPLY_RESET &&
                reply->type == REDIS_REPLY_STATUS &&
                strncmp(reply->str, "RESET", reply->len) == 0)
            {
                handleReset(ac);
            } else if (cb->pending_replies == PENDING_REPLY_MONITOR &&
                       reply->type == REDIS_REPLY_STATUS &&
                       strncmp(reply->str, "OK", reply->len) == 0)
            {
                /* Set monitor flag and callback, freeing any old callback. */
                c->flags |= REDIS_MONITORING;
                if (ac->monitor_cb != NULL) {
                    callbackDecrRefCount(ac, ac->monitor_cb);
                }
                ac->monitor_cb = cb;
                callbackIncrRefCount(ac, cb);
            }

            /* Invoke callback */
            __redisRunCallback(ac, cb, reply);
            cb->pending_replies = 0;
        }

        if (cb != NULL) {
            if (cb->pending_replies != 0) {
                /* The command needs more repies. Put it first in queue. */
                __redisUnshiftCallback(&ac->replies, cb);
            } else {
                callbackDecrRefCount(ac, cb);
            }
        }

        /* Proceed with free'ing when redisAsyncFree() was called. */
        if (c->flags & REDIS_FREEING) {
            __redisAsyncFree(ac);
            return;
        }
    }

    /* Disconnect when there was an error reading the reply */
    if (status != REDIS_OK)
        __redisAsyncDisconnect(ac);
}

static void __redisAsyncHandleConnectFailure(redisAsyncContext *ac) {
    __redisRunConnectCallback(ac, REDIS_ERR);
    __redisAsyncDisconnect(ac);
}

/* Internal helper function to detect socket status the first time a read or
 * write event fires. When connecting was not successful, the connect callback
 * is called with a REDIS_ERR status and the context is free'd. */
static int __redisAsyncHandleConnect(redisAsyncContext *ac) {
    int completed = 0;
    redisContext *c = &(ac->c);

    if (redisCheckConnectDone(c, &completed) == REDIS_ERR) {
        /* Error! */
        if (redisCheckSocketError(c) == REDIS_ERR)
            __redisAsyncCopyError(ac);
        __redisAsyncHandleConnectFailure(ac);
        return REDIS_ERR;
    } else if (completed == 1) {
        /* connected! */
        if (c->connection_type == REDIS_CONN_TCP &&
            redisSetTcpNoDelay(c) == REDIS_ERR) {
            __redisAsyncHandleConnectFailure(ac);
            return REDIS_ERR;
        }

        /* flag us as fully connect, but allow the callback
         * to disconnect.  For that reason, permit the function
         * to delete the context here after callback return.
         */
        c->flags |= REDIS_CONNECTED;
        __redisRunConnectCallback(ac, REDIS_OK);
        if ((ac->c.flags & REDIS_DISCONNECTING)) {
            redisAsyncDisconnect(ac);
            return REDIS_ERR;
        } else if ((ac->c.flags & REDIS_FREEING)) {
            redisAsyncFree(ac);
            return REDIS_ERR;
        }
        return REDIS_OK;
    } else {
        return REDIS_OK;
    }
}

void redisAsyncRead(redisAsyncContext *ac) {
    redisContext *c = &(ac->c);

    if (redisBufferRead(c) == REDIS_ERR) {
        __redisAsyncDisconnect(ac);
    } else {
        /* Always re-schedule reads */
        _EL_ADD_READ(ac);
        redisProcessCallbacks(ac);
    }
}

/* This function should be called when the socket is readable.
 * It processes all replies that can be read and executes their callbacks.
 */
void redisAsyncHandleRead(redisAsyncContext *ac) {
    redisContext *c = &(ac->c);
    /* must not be called from a callback */
    assert(!(c->flags & REDIS_IN_CALLBACK));

    if (!(c->flags & REDIS_CONNECTED)) {
        /* Abort connect was not successful. */
        if (__redisAsyncHandleConnect(ac) != REDIS_OK)
            return;
        /* Try again later when the context is still not connected. */
        if (!(c->flags & REDIS_CONNECTED))
            return;
    }

    c->funcs->async_read(ac);
}

void redisAsyncWrite(redisAsyncContext *ac) {
    redisContext *c = &(ac->c);
    int done = 0;

    if (redisBufferWrite(c,&done) == REDIS_ERR) {
        __redisAsyncDisconnect(ac);
    } else {
        /* Continue writing when not done, stop writing otherwise */
        if (!done)
            _EL_ADD_WRITE(ac);
        else
            _EL_DEL_WRITE(ac);

        /* Always schedule reads after writes */
        _EL_ADD_READ(ac);
    }
}

void redisAsyncHandleWrite(redisAsyncContext *ac) {
    redisContext *c = &(ac->c);
    /* must not be called from a callback */
    assert(!(c->flags & REDIS_IN_CALLBACK));

    if (!(c->flags & REDIS_CONNECTED)) {
        /* Abort connect was not successful. */
        if (__redisAsyncHandleConnect(ac) != REDIS_OK)
            return;
        /* Try again later when the context is still not connected. */
        if (!(c->flags & REDIS_CONNECTED))
            return;
    }

    c->funcs->async_write(ac);
}

void redisAsyncHandleTimeout(redisAsyncContext *ac) {
    redisContext *c = &(ac->c);
    redisCallback *cb;
    /* must not be called from a callback */
    assert(!(c->flags & REDIS_IN_CALLBACK));

    if ((c->flags & REDIS_CONNECTED)) {
        if (ac->replies.head == NULL) {
            /* Nothing to do - just an idle timeout */
            return;
        }

        if (!ac->c.command_timeout ||
            (!ac->c.command_timeout->tv_sec && !ac->c.command_timeout->tv_usec)) {
            /* A belated connect timeout arriving, ignore */
            return;
        }
    }

    if (!c->err) {
        __redisSetError(c, REDIS_ERR_TIMEOUT, "Timeout");
        __redisAsyncCopyError(ac);
    }

    if (!(c->flags & REDIS_CONNECTED)) {
        __redisRunConnectCallback(ac, REDIS_ERR);
    }

    while (__redisShiftCallback(&ac->replies, &cb) == REDIS_OK) {
        __redisRunCallback(ac, cb, NULL);
        callbackDecrRefCount(ac, cb);
    }

    /**
     * TODO: Don't automatically sever the connection,
     * rather, allow to ignore <x> responses before the queue is clear
     */
    __redisAsyncDisconnect(ac);
}

/* Sets a pointer to the first argument and its length starting at p. Returns
 * the number of bytes to skip to get to the following argument. */
static const char *nextArgument(const char *start, const char **str, size_t *len) {
    const char *p = start;
    if (p[0] != '$') {
        p = strchr(p,'$');
        if (p == NULL) return NULL;
    }

    *len = (int)strtol(p+1,NULL,10);
    p = strchr(p,'\r');
    assert(p);
    *str = p+2;
    return p+2+(*len)+2;
}

static int isPubsubCommand(const char *cmd, size_t len) {
    if (len < strlen("subscribe") || len > strlen("punsubscribe"))
        return 0; /* fast path */
    return
        strncasecmp(cmd, "subscribe", len) == 0 ||
        strncasecmp(cmd, "unsubscribe", len) == 0 ||
        strncasecmp(cmd, "psubscribe", len) == 0 ||
        strncasecmp(cmd, "punsubscribe", len) == 0 ||
        strncasecmp(cmd, "ssubscribe", len) == 0 ||
        strncasecmp(cmd, "sunsubscribe", len) == 0;
}

/* Helper function for the redisAsyncCommand* family of functions. Writes a
 * formatted command to the output buffer and registers the provided callback
 * function with the context. */
static int __redisAsyncCommand(redisAsyncContext *ac, redisCallbackFn *fn,
                               redisFinalizerCallback *finalizer, void *privdata,
                               const char *cmd, size_t len) {
    redisContext *c = &(ac->c);
    redisCallback *cb;
    const char *cstr, *astr;
    size_t clen, alen;
    const char *p;

    /* Don't accept new commands when the connection is about to be closed. */
    if (c->flags & (REDIS_DISCONNECTING | REDIS_FREEING)) return REDIS_ERR;

    /* Setup callback */
    cb = hi_malloc(sizeof(*cb));
    if (cb == NULL)
        goto oom;
    cb->fn = fn;
    cb->finalizer = finalizer;
    cb->privdata = privdata;
    cb->refcount = 1;
    cb->pending_replies = 1; /* Most commands have exactly 1 reply. */

    /* Find out which command will be appended. */
    p = nextArgument(cmd,&cstr,&clen);
    assert(p != NULL);

    if (isPubsubCommand(cstr, clen)) {
        /* The number of replies is the number of channels or patterns. Count
         * the arguments. */
        cb->pending_replies = 0;
        while ((p = nextArgument(p, &astr, &alen)) != NULL) {
            cb->pending_replies++;
        }
        if (cb->pending_replies == 0) {
            /* No channels specified means unsubscribe all. (This can happens
             * for SUBSCRIBE, but it is an error and then the value of pending
             * replies doesn't matter.) */
            cb->pending_replies = PENDING_REPLY_UNSUBSCRIBE_ALL;
        }
        c->flags |= REDIS_SUBSCRIBED;
        ac->sub.pending_commands++;
    } else if (strncasecmp(cstr, "monitor", clen) == 0) {
        cb->pending_replies = PENDING_REPLY_MONITOR;
    } else if (strncasecmp(cstr, "reset", clen) == 0) {
        cb->pending_replies = PENDING_REPLY_RESET;
    }

    if (__redisPushCallback(&ac->replies, cb) != REDIS_OK)
        goto oom;

    __redisAppendCommand(c,cmd,len);

    /* Always schedule a write when the write buffer is non-empty */
    _EL_ADD_WRITE(ac);

    return REDIS_OK;
oom:
    __redisSetError(&(ac->c), REDIS_ERR_OOM, "Out of memory");
    __redisAsyncCopyError(ac);
    callbackDecrRefCount(ac, cb);
    return REDIS_ERR;
}

int redisvAsyncCommand(redisAsyncContext *ac, redisCallbackFn *fn, void *privdata, const char *format, va_list ap) {
    return redisvAsyncCommandWithFinalizer(ac, fn, NULL, privdata, format, ap);
}

int redisvAsyncCommandWithFinalizer(redisAsyncContext *ac, redisCallbackFn *fn, redisFinalizerCallback *finalizer,
                                    void *privdata, const char *format, va_list ap) {
    char *cmd;
    int len;
    int status;
    len = redisvFormatCommand(&cmd,format,ap);

    /* We don't want to pass -1 or -2 to future functions as a length. */
    if (len < 0)
        return REDIS_ERR;

    status = __redisAsyncCommand(ac,fn,finalizer,privdata,cmd,len);
    hi_free(cmd);
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

int redisAsyncCommandWithFinalizer(redisAsyncContext *ac, redisCallbackFn *fn, redisFinalizerCallback *finalizer,
                                   void *privdata, const char *format, ...) {
    va_list ap;
    int status;
    va_start(ap,format);
    status = redisvAsyncCommandWithFinalizer(ac,fn,finalizer,privdata,format,ap);
    va_end(ap);
    return status;
}

int redisAsyncCommandArgv(redisAsyncContext *ac, redisCallbackFn *fn, void *privdata, int argc, const char **argv, const size_t *argvlen) {
    return redisAsyncCommandArgvWithFinalizer(ac, fn, NULL, privdata, argc, argv, argvlen);
}

int redisAsyncCommandArgvWithFinalizer(redisAsyncContext *ac, redisCallbackFn *fn, redisFinalizerCallback *finalizer,
                                       void *privdata, int argc, const char **argv, const size_t *argvlen) {
    sds cmd;
    long long len;
    int status;
    len = redisFormatSdsCommandArgv(&cmd,argc,argv,argvlen);
    if (len < 0)
        return REDIS_ERR;
    status = __redisAsyncCommand(ac,fn,finalizer,privdata,cmd,len);
    sdsfree(cmd);
    return status;
}

int redisAsyncFormattedCommand(redisAsyncContext *ac, redisCallbackFn *fn, void *privdata, const char *cmd, size_t len) {
    int status = __redisAsyncCommand(ac,fn,NULL,privdata,cmd,len);
    return status;
}

int redisAsyncFormattedCommandWithFinalizer(redisAsyncContext *ac, redisCallbackFn *fn, redisFinalizerCallback *finalizer,
                                            void *privdata, const char *cmd, size_t len) {
    int status = __redisAsyncCommand(ac,fn,finalizer,privdata,cmd,len);
    return status;
}

redisAsyncPushFn *redisAsyncSetPushCallback(redisAsyncContext *ac, redisAsyncPushFn *fn) {
    redisAsyncPushFn *old = ac->push_cb;
    ac->push_cb = fn;
    return old;
}

int redisAsyncSetTimeout(redisAsyncContext *ac, struct timeval tv) {
    if (!ac->c.command_timeout) {
        ac->c.command_timeout = hi_calloc(1, sizeof(tv));
        if (ac->c.command_timeout == NULL) {
            __redisSetError(&ac->c, REDIS_ERR_OOM, "Out of memory");
            __redisAsyncCopyError(ac);
            return REDIS_ERR;
        }
    }

    if (tv.tv_sec != ac->c.command_timeout->tv_sec ||
        tv.tv_usec != ac->c.command_timeout->tv_usec)
    {
        *ac->c.command_timeout = tv;
    }

    return REDIS_OK;
}
