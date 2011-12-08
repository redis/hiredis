#ifndef __HIREDIS_GLIB_H__
#define __HIREDIS_GLIB_H__
#include <glib.h>
#include "../hiredis.h"
#include "../async.h"

typedef struct redisGlibEvents {
    GSource source;
    GPollFD poll_fd;
    gboolean removed;

    redisAsyncContext *context;
} redisGlibEvents;

/* Stop polling the events file descriptor source */
static void redisGlibSourceRemove(redisGlibEvents *e) {
    e->removed = TRUE;
    g_source_remove_poll((GSource*)e, &e->poll_fd);
}

static gboolean redisGlibSourcePrepare(GSource *source, gint *timeout) {
    ((void)source);
    /* Need to wait for poll() to be called before we known if any events
     * need to be processed. It doesn't matter how long poll() blocks for.
     */
    *timeout = -1;
    return FALSE;
}

static gboolean redisGlibSourceCheck(GSource *source) {
    redisGlibEvents *e = (redisGlibEvents*)source;
    /* The source is checked regardless of the result of the polling.
     * Only signal that the source is ready if poll() indicated so.
     */
    return (e->poll_fd.events & e->poll_fd.revents);
}

static gboolean redisGlibSourceDispatch(GSource *source,
                                        GSourceFunc callback,
                                        gpointer user_data) {
    ((void)callback);
    ((void)user_data);
    redisGlibEvents *e = (redisGlibEvents*)source;
    ushort revents = e->poll_fd.revents;
    gboolean ret = TRUE;

    if(revents & (G_IO_NVAL|G_IO_HUP|G_IO_ERR)) {
        /* G_IO_NVAL may happen if the file descriptor is closed while
         * the GLib source is active. This and other error conditions
         * need to be propagated to hiredis async handlers.
         */
        redisGlibSourceRemove(e);
        ret = FALSE;
    }

    if(!ret || (revents & G_IO_IN))
        redisAsyncHandleRead(e->context);

    if(!ret || (revents & G_IO_OUT))
        redisAsyncHandleWrite(e->context);

    return ret;
}

static GSourceFuncs redisGlibSourceFuncs = {
    redisGlibSourcePrepare,
    redisGlibSourceCheck,
    redisGlibSourceDispatch,
    NULL,
    (GSourceFunc)NULL,
    (GSourceDummyMarshal)NULL
};

/* Add an IO condition to source file descriptor poll. */ 
static void redisGlibAddPoll(GSource *source,
                             GPollFD *pollfd,
                             GIOCondition cond) {
    /* Only update poll conditions when they are different */
    if(pollfd->events & cond)
        return;

    g_source_remove_poll(source, pollfd);
    pollfd->events |= cond;
    g_source_add_poll(source, pollfd);
}

/* Remove an IO condition from source file descriptor poll. */ 
static void redisGlibRemovePoll(GSource *source,
                                GPollFD *pollfd,
                                GIOCondition cond) {
    /* Only update poll conditions when they are different */
    if(!(pollfd->events & cond))
        return;

    g_source_remove_poll(source, pollfd);
    pollfd->events &= ~cond;
    g_source_add_poll(source, pollfd);
}

static void redisGlibAddRead(void *privdata) {
    redisGlibEvents *e = (redisGlibEvents*)privdata;
    redisGlibAddPoll((GSource*)e, &e->poll_fd, G_IO_IN);
}

static void redisGlibDelRead(void *privdata) {
    redisGlibEvents *e = (redisGlibEvents*)privdata;
    redisGlibRemovePoll((GSource*)e, &e->poll_fd, G_IO_IN);
}

static void redisGlibAddWrite(void *privdata) {
    redisGlibEvents *e = (redisGlibEvents*)privdata;
    redisGlibAddPoll((GSource*)e, &e->poll_fd, G_IO_OUT);
}

static void redisGlibDelWrite(void *privdata) {
    redisGlibEvents *e = (redisGlibEvents*)privdata;
    redisGlibRemovePoll((GSource*)e, &e->poll_fd, G_IO_OUT);
}

static void redisGlibCleanup(void *privdata) {
    redisGlibEvents *e = (redisGlibEvents*)privdata;

    redisGlibSourceRemove(e);
    g_source_unref((GSource*)e);
}

static int redisGlibAttach(GMainContext *ctx, redisAsyncContext *ac) {
    redisContext *c = &(ac->c);
    redisGlibEvents *e;

    /* Nothing should be attached when something is already attached */
    if (ac->ev.data != NULL)
        return REDIS_ERR;

    /* Create container for context and r/w events */
    e = (redisGlibEvents*)g_source_new(&redisGlibSourceFuncs,
                                       sizeof(redisGlibEvents));
    e->context = ac;

    e->poll_fd.fd = c->fd;
    e->poll_fd.events = G_IO_NVAL|G_IO_HUP|G_IO_ERR;

    g_source_attach((GSource*)e, ctx);

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
