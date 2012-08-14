#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "spawn.h"

int pidc = 0;
pid_t *pidv = NULL;

void spawn_init(void) {
    assert(pidc == 0);
    assert(pidv == NULL);
}

void spawn_destroy(void) {
    pid_t pid;
    int i;
    int rv;

    /* Kill children */
    for (i = 0; i < pidc; i++) {
        int status;

        pid = waitpid(pidv[i], &status, WNOHANG);
        if (pid) {
            continue;
        }

        rv = kill(pidv[i], SIGKILL);
        if (rv == -1) {
            fprintf(stderr, "kill: %s\n", strerror(errno));
            exit(1);
        }

        pid = waitpid(pidv[i], &status, 0);
        assert(pid == pidv[i]);
    }

    free(pidv);
    pidc = 0;
    pidv = NULL;
}

void spawn(spawn_function *fn, void *ptr) {
    pid_t pid;

    pid = fork();
    if (pid) {
        pidv = realloc(pidv, sizeof(pid_t) * (pidc + 1));
        pidv[pidc++] = pid;
    } else {
        fn(ptr);
        exit(0);
    }

    struct timespec ts;

    ts.tv_sec = 0;
    ts.tv_nsec = 1000 * 1000;

    /* Give the fork some time to boot... */
    nanosleep(&ts, NULL);
}
