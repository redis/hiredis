#ifndef __HIREDIS_GLIB_H__
#define __HIREDIS_GLIB_H__
#include <glib.h>
#include "../hiredis.h"
#include "../async.h"

typedef struct redisGlibEvents {

    redisAsyncContext *context;

    GMainContext *main_context;
    GIOChannel *channel;

    guint err_source_id;
    guint read_source_id;
    guint write_source_id;

} redisGlibEvents;

/* g_io_add_watch_to_context is, essentially the same as g_io_add_watch
 * but with the ability to specify a context to attach the IO watch to.
 * Calling g_io_add_watch_to_context with context equal to NULL is equivalent
 * to calling g_io_add_watch.
 */
static guint g_io_add_watch_to_context(GIOChannel *channel,
                                       GIOCondition condition,
                                       GIOFunc func,
                                       gpointer user_data,
                                       GMainContext *context)
{
    GSource *source;
    guint id;

    g_return_val_if_fail(channel != NULL, 0);

    source = g_io_create_watch(channel, condition);
    g_source_set_callback(source, (GSourceFunc)func, user_data, NULL);

    id = g_source_attach(source, context);
    g_source_unref(source);

    return id;
}

static gboolean redisGlibIOCallback(GIOChannel *source,
                                    GIOCondition condition,
                                    gpointer privdata)
{
    redisGlibEvents *e = (redisGlibEvents*)privdata;
    gboolean ret = TRUE;

    if(condition & (G_IO_NVAL|G_IO_HUP|G_IO_ERR)) {
        // An error condition occured. Propagate this
        // inforation to read/write async handlers.
        condition |= G_IO_IN|G_IO_OUT;
        ret = FALSE;
    }

    if(e->read_source_id && (condition & G_IO_IN))
        redisAsyncHandleRead(e->context);

    if(e->write_source_id && (condition & G_IO_OUT))
        redisAsyncHandleWrite(e->context);

    if(ret == FALSE) {
        // In case of errors, event sources are destroyed.
        e->err_source_id = 0;
        e->read_source_id = 0;
        e->write_source_id = 0;
    }

    return ret;
}

static void redisGlibAddRead(void *privdata) {
    redisGlibEvents *e = (redisGlibEvents*)privdata;

    if(e->read_source_id)
        return;

    e->read_source_id = g_io_add_watch_to_context(e->channel,
                                                  G_IO_IN,
                                                  redisGlibIOCallback,
                                                  e,
                                                  e->main_context);
}

static void redisGlibDelRead(void *privdata) {
    redisGlibEvents *e = (redisGlibEvents*)privdata;
    GSource *source;

    if(!e->read_source_id)
        return;

    source = g_main_context_find_source_by_id(e->main_context,
                                              e->read_source_id);
    g_source_destroy(source);
    e->read_source_id = 0;
}

static void redisGlibAddWrite(void *privdata) {
    redisGlibEvents *e = (redisGlibEvents*)privdata;

    if(e->write_source_id)
        return;

    e->write_source_id = g_io_add_watch_to_context(e->channel,
                                                   G_IO_OUT,
                                                   redisGlibIOCallback,
                                                   e,
                                                   e->main_context);
}

static void redisGlibDelWrite(void *privdata) {
    redisGlibEvents *e = (redisGlibEvents*)privdata;
    GSource *source;

    if(!e->write_source_id)
        return;

    source = g_main_context_find_source_by_id(e->main_context,
                                              e->write_source_id);
    g_source_destroy(source);
    e->write_source_id = 0;
}

static void redisGlibCleanup(void *privdata) {
    redisGlibEvents *e = (redisGlibEvents*)privdata;
    GSource *source;

    if(e->err_source_id) {
        source = g_main_context_find_source_by_id(e->main_context,
                                                  e->err_source_id);
        g_source_destroy(source);
    }

    if(e->read_source_id) {
        source = g_main_context_find_source_by_id(e->main_context,
                                                  e->read_source_id);
        g_source_destroy(source);
    }

    if(e->write_source_id) {
        source = g_main_context_find_source_by_id(e->main_context,
                                                  e->write_source_id);
        g_source_destroy(source);
    }

    g_io_channel_unref(e->channel);

    free(e);
}

static int redisGlibAttach(GMainContext *ctx, redisAsyncContext *ac) {
    redisContext *c = &(ac->c);
    redisGlibEvents *e;

    /* Nothing should be attached when something is already attached */
    if (ac->ev.data != NULL)
        return REDIS_ERR;

    /* Create container for context and r/w events */
    e = (redisGlibEvents*)malloc(sizeof(*e));
    e->context = ac;
    e->main_context = ctx;

    e->channel = g_io_channel_unix_new(c->fd);
    g_io_channel_set_close_on_unref(e->channel, FALSE);

    e->err_source_id = g_io_add_watch_to_context(e->channel,
                                                 G_IO_NVAL|G_IO_HUP|G_IO_ERR,
                                                 redisGlibIOCallback,
                                                 e,
                                                 e->main_context);

    /* Register functions to start/stop listening for events */
    ac->ev.addRead = redisGlibAddRead;
    ac->ev.delRead = redisGlibDelRead;
    ac->ev.addWrite = redisGlibAddWrite;
    ac->ev.delWrite = redisGlibDelWrite;
    ac->ev.cleanup = redisGlibCleanup;
    ac->ev.data = e;

    return REDIS_OK;
}


#endif
