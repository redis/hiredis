#ifndef __REDIS_PARSER_H
#define __REDIS_PARSER_H

#include <stdint.h>

#define REDIS_STRING_T 1
#define REDIS_ARRAY_T 2
#define REDIS_INTEGER_T 3
#define REDIS_NIL_T 4
#define REDIS_STATUS_T 5
#define REDIS_ERROR_T 6

typedef struct redis_parser_cb_s redis_parser_cb_t;
typedef struct redis_protocol_s redis_protocol_t;
typedef struct redis_parser_s redis_parser_t;

typedef int (*redis_string_cb)(redis_parser_t *, redis_protocol_t *, const char *, size_t);
typedef int (*redis_array_cb)(redis_parser_t *, redis_protocol_t *, size_t);
typedef int (*redis_integer_cb)(redis_parser_t *, redis_protocol_t *, int64_t);
typedef int (*redis_nil_cb)(redis_parser_t *, redis_protocol_t *);

struct redis_parser_cb_s {
    redis_string_cb on_string;
    redis_array_cb on_array;
    redis_integer_cb on_integer;
    redis_nil_cb on_nil;
};

struct redis_protocol_s {
    size_t poff; /* protocol offset */
    size_t plen; /* protocol length */
    size_t coff; /* content offset */
    size_t clen; /* content length */

    unsigned char type; /* payload type */
    int64_t remaining; /* remaining bulk bytes/nested objects */

    const redis_protocol_t* parent; /* when nested, parent object */
    void *data; /* user data */
};

struct redis_parser_s {
    /* private: callbacks */
    const redis_parser_cb_t *callbacks;

    /* private: number of consumed bytes for a single message */
    size_t nread;

    /* private: protocol_t stack (multi-bulk, nested multi-bulk) */
    redis_protocol_t stack[3];
    int stackidx;

    /* private: parser state */
    unsigned char state;

    /* private: temporary integer (integer reply, bulk length) */
    struct {
        int neg;
        uint64_t ui64;
    } i64;
};

void redis_parser_init(redis_parser_t *parser, const redis_parser_cb_t *callbacks);
size_t redis_parser_execute(redis_parser_t *parser, redis_protocol_t **dst, const char *buf, size_t len);

#endif // __REDIS_PARSER_H
