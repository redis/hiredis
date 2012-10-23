#ifndef HIREDIS_OBJECT_H
#define HIREDIS_OBJECT_H 1

#include "parser.h"

typedef struct redis_object_s redis_object;

struct redis_object_s {
    int type; /* Object type */
    int64_t integer; /* Value for REDIS_INTEGER */
    char *str; /* REDIS_STRING, REDIS_STATUS, REDIS_ERROR */
    unsigned int len; /* String length */
    struct redis_object_s **element; /* Actual elements in REDIS_ARRAY */
    unsigned int elements; /* Number of elements REDIS_ARRAY */
};

void redis_object_free(redis_object **obj);
extern redis_parser_callbacks redis_object_parser_callbacks;

#endif
