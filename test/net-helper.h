#ifndef TEST_NET_HELPER
#define TEST_NET_HELPER 1

#include "../handle.h"

typedef struct accept_and_ignore_args_s accept_and_ignore_args;

struct accept_and_ignore_args_s {
    redis_address address;
};

void accept_and_ignore(void *ptr);

#endif
