#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include "net-helper.h"

void accept_and_ignore(void *ptr) {
    accept_and_ignore_args *args = ptr;
    int fd, rv;

    fd = socket(args->address.sa_family, SOCK_STREAM, 0);
    if (fd == -1) {
        fprintf(stderr, "socket: %s\n", strerror(errno));
        exit(1);
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

        /* Ignore the new connection */
    }
}
