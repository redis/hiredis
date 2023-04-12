#ifndef __HIREDIS_INTERNAL_H
#define __HIREDIS_INTERNAL_H

#include "hiredis.h"

int redisContextUpdateCommandTimeout(redisContext *c, const struct timeval *timeout);
int redisContextUpdateConnectTimeout(redisContext *c, const struct timeval *timeout);
void __redisSetError(redisContext *c, int type, const char *str);
int __redisAppendCommand(redisContext *c, const char *cmd, size_t len);

#endif
