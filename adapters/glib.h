#ifndef __HIREDIS_GLIB_H__
#define __HIREDIS_GLIB_H__
#include <stdlib.h>
#include <sys/types.h>
#include <glib.h>
#include "../hiredis.h"
#include "../async.h"

#define _G_UNUSED(x) ((void)x)

typedef struct redisGlibEvents {
    redisAsyncContext *context;
    int reading, writing;
    GIOChannel *channel;
    guint readw,writew;
} redisGlibEvents;

static gboolean redisGlibEvent(GIOChannel *gio, GIOCondition condition, gpointer data)
{
	_G_UNUSED(gio);
	redisGlibEvents *e = (redisGlibEvents*)data;

	if (condition & G_IO_IN) {
		redisAsyncHandleRead(e->context);
	} else {
		redisAsyncHandleWrite(e->context);
	}
	return TRUE;
}



static void redisGlibAddRead(void *privdata) {
    redisGlibEvents *e = (redisGlibEvents*)privdata;
    if (!e->reading) {
        e->reading = 1;
	e->readw = g_io_add_watch(e->channel, G_IO_IN, redisGlibEvent, e);
    }
}

static void redisGlibDelRead(void *privdata) {
    redisGlibEvents *e = (redisGlibEvents*)privdata;
    if (e->reading) {
        e->reading = 0;
	g_source_remove(e->readw);
    }
}

static void redisGlibAddWrite(void *privdata) {
    redisGlibEvents *e = (redisGlibEvents*)privdata;
    if (!e->writing) {
        e->writing = 1;
	e->writew = g_io_add_watch(e->channel, G_IO_OUT, redisGlibEvent, e);
    }
}

static void redisGlibDelWrite(void *privdata) {
    redisGlibEvents *e = (redisGlibEvents*)privdata;
    if (e->writing) {
        e->writing = 0;
	g_source_remove(e->writew);
    }
}

static void redisGlibCleanup(void *privdata) {
    redisGlibEvents *e = (redisGlibEvents*)privdata;
    redisGlibDelRead(privdata);
    redisGlibDelWrite(privdata);
    g_io_channel_unref(e->channel);
    g_free(e);
}

static int redisGlibAttach(GMainLoop *loop, redisAsyncContext *ac) {
    redisContext *c = &(ac->c);
    redisGlibEvents *e;

    _G_UNUSED(loop);
    /* Nothing should be attached when something is already attached */
    if (ac->ev.data != NULL)
        return REDIS_ERR;

    /* Create container for context and r/w events */
    e = (redisGlibEvents*)g_malloc(sizeof(*e));
    e->context = ac;
    e->reading = e->writing = 0;
    e->channel = g_io_channel_unix_new(c->fd);

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
