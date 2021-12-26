
// A libuv timeout example
// by github dot com slash michael-grunder
// many thanks!

// usage: make hiredis-example-uvtm && ./examples/hiredis-example-uvtm

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include <hiredis.h>
#include <async.h>
#include <adapters/libuv.h>
#include <time.h>
#include <sys/time.h>

void sleepCb(redisAsyncContext *c, void *r, void *privdata) {
    printf("sleep CB\n");
}

int main (int argc, char **argv) {
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif

    uv_loop_t* loop = uv_default_loop();

    redisAsyncContext *c = redisAsyncConnect("127.0.0.1", 6379);
    if (c->err) {
        /* Let *c leak for now... */
        printf("Error: %s\n", c->errstr);
        return 1;
    }

    redisLibuvAttach(c,loop);
    redisAsyncSetTimeout(c, (struct timeval){ .tv_sec = 1, .tv_usec = 0});
    redisAsyncCommand(c, sleepCb, NULL, "DEBUG SLEEP %f", 1.5);
    uv_run(loop, UV_RUN_DEFAULT);
    return 0;
}
