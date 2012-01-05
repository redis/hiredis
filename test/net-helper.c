#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include "net-helper.h"

void run_server(void *ptr) {
    run_server_args *args = ptr;
    int family = args->address.sa_family;
    int fd;
    int rv;

    fd = socket(family, SOCK_STREAM, 0);
    if (fd == -1) {
        fprintf(stderr, "socket: %s\n", strerror(errno));
        exit(1);
    }

    if (family == AF_INET || family == AF_INET6) {
        int on = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == -1) {
            fprintf(stderr, "setsockopt: %s\n", strerror(errno));
            exit(1);
        }
    }

    rv = bind(fd, &args->address.sa_addr.addr, args->address.sa_addrlen);
    if (rv == -1) {
        fprintf(stderr, "bind: %s\n", strerror(errno));
        exit(1);
    }

    rv = listen(fd, 128);
    if (rv == -1) {
        fprintf(stderr, "listen: %s\n", strerror(errno));
        exit(1);
    }

    while (1) {
        redis_address remote;

        remote.sa_addrlen = sizeof(remote.sa_addr);

        rv = accept(fd, &remote.sa_addr.addr, &remote.sa_addrlen);
        if (rv == -1) {
            fprintf(stderr, "accept: %s\n", strerror(errno));
            exit(1);
        }

        /* Execute specified function, if given */
        if (args->fn.ptr != NULL) {
            args->fn.ptr(rv, args->fn.data);
        }
    }
}
