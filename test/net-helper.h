#ifndef TEST_NET_HELPER
#define TEST_NET_HELPER 1

#include "../handle.h"

typedef struct run_server_args_s run_server_args;

struct run_server_args_s {
    redis_address address;
    struct {
        void (*ptr)(int fd, void *data);
        void *data;
    } fn;
};

void run_server(void *ptr);

#endif
