/*
 * Copyright (c) 2012, Christian Hergert <chris at dronelabs dot com>
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

#ifndef __HIREDIS_GLIB_H__
#define __HIREDIS_GLIB_H__

#include <glib.h>

#include "../hiredis.h"
#include "../async.h"

typedef struct
{
    GSource source;
    redisAsyncContext *ac;
    GPollFD poll_fd;
} RedisSource;

static void
redis_source_add_read (gpointer data)
{
    RedisSource *source = (RedisSource *)data;
    g_return_if_fail(source);
    source->poll_fd.events |= G_IO_IN;
    g_main_context_wakeup(g_source_get_context((GSource *)data));
}

static void
redis_source_del_read (gpointer data)
{
    RedisSource *source = (RedisSource *)data;
    g_return_if_fail(source);
    source->poll_fd.events &= ~G_IO_IN;
    g_main_context_wakeup(g_source_get_context((GSource *)data));
}

static void
redis_source_add_write (gpointer data)
{
    RedisSource *source = (RedisSource *)data;
    g_return_if_fail(source);
    source->poll_fd.events |= G_IO_OUT;
    g_main_context_wakeup(g_source_get_context((GSource *)data));
}

static void
redis_source_del_write (gpointer data)
{
    RedisSource *source = (RedisSource *)data;
    g_return_if_fail(source);
    source->poll_fd.events &= ~G_IO_OUT;
    g_main_context_wakeup(g_source_get_context((GSource *)data));
}

static void
redis_source_cleanup (gpointer data)
{
    RedisSource *source = (RedisSource *)data;

    g_return_if_fail(source);

    redis_source_del_read(source);
    redis_source_del_write(source);
    /*
     * It is not our responsibility to remove ourself from the
     * current main loop. However, we will remove the GPollFD.
     */
    if (source->poll_fd.fd >= 0) {
        g_source_remove_poll((GSource *)data, &source->poll_fd);
        source->poll_fd.fd = -1;
    }
}

static gboolean
redis_source_prepare (GSource *source,
                      gint    *timeout_)
{
    RedisSource *redis = (RedisSource *)source;
    *timeout_ = -1;
    return !!(redis->poll_fd.events & redis->poll_fd.revents);
}

static gboolean
redis_source_check (GSource *source)
{
    RedisSource *redis = (RedisSource *)source;
    return !!(redis->poll_fd.events & redis->poll_fd.revents);
}

static gboolean
redis_source_dispatch (GSource      *source,
                       GSourceFunc   callback,
                       gpointer      user_data)
{
    RedisSource *redis = (RedisSource *)source;

    if ((redis->poll_fd.revents & G_IO_OUT)) {
        redisAsyncHandleWrite(redis->ac);
        redis->poll_fd.revents &= ~G_IO_OUT;
    }

    if ((redis->poll_fd.revents & G_IO_IN)) {
        redisAsyncHandleRead(redis->ac);
        redis->poll_fd.revents &= ~G_IO_IN;
    }

    if (callback) {
        return callback(user_data);
    }

    return TRUE;
}

static void
redis_source_finalize (GSource *source)
{
    RedisSource *redis = (RedisSource *)source;

    if (redis->poll_fd.fd >= 0) {
        g_source_remove_poll(source, &redis->poll_fd);
        redis->poll_fd.fd = -1;
    }
}

static GSource *
redis_source_new (redisAsyncContext *ac)
{
    static GSourceFuncs source_funcs = {
        .prepare  = redis_source_prepare,
        .check     = redis_source_check,
        .dispatch = redis_source_dispatch,
        .finalize = redis_source_finalize,
    };
    redisContext *c = &ac->c;
    RedisSource *source;

    g_return_val_if_fail(ac != NULL, NULL);

    source = (RedisSource *)g_source_new(&source_funcs, sizeof *source);
    if (source == NULL)
        return NULL;

    source->ac = ac;
    source->poll_fd.fd = c->fd;
    source->poll_fd.events = 0;
    source->poll_fd.revents = 0;
    g_source_add_poll((GSource *)source, &source->poll_fd);

    ac->ev.addRead = redis_source_add_read;
    ac->ev.delRead = redis_source_del_read;
    ac->ev.addWrite = redis_source_add_write;
    ac->ev.delWrite = redis_source_del_write;
    ac->ev.cleanup = redis_source_cleanup;
    ac->ev.data = source;

    return (GSource *)source;
}

#endif /* __HIREDIS_GLIB_H__ */
