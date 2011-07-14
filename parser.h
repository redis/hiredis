#ifndef __REDIS_PARSER_H
#define __REDIS_PARSER_H

#include <stdint.h>

/* Compat */
#define REDIS_REPLY_STRING 1
#define REDIS_REPLY_ARRAY 2
#define REDIS_REPLY_INTEGER 3
#define REDIS_REPLY_NIL 4
#define REDIS_REPLY_STATUS 5
#define REDIS_REPLY_ERROR 6

#define REDIS_STRING_T REDIS_REPLY_STRING
#define REDIS_ARRAY_T REDIS_REPLY_ARRAY
#define REDIS_INTEGER_T REDIS_REPLY_INTEGER
#define REDIS_NIL_T REDIS_REPLY_NIL
#define REDIS_STATUS_T REDIS_REPLY_STATUS
#define REDIS_ERROR_T REDIS_REPLY_ERROR

typedef struct redis_parser_callbacks_s redis_parser_callbacks_t;
typedef struct redis_protocol_s redis_protocol_t;
typedef struct redis_parser_s redis_parser_t;

typedef int (*redis_string_cb)(redis_parser_t *, redis_protocol_t *, const char *, size_t);
typedef int (*redis_array_cb)(redis_parser_t *, redis_protocol_t *, size_t);
typedef int (*redis_integer_cb)(redis_parser_t *, redis_protocol_t *, int64_t);
typedef int (*redis_nil_cb)(redis_parser_t *, redis_protocol_t *);

struct redis_parser_callbacks_s {
    redis_string_cb on_string;
    redis_array_cb on_array;
    redis_integer_cb on_integer;
    redis_nil_cb on_nil;
};

#define REDIS_PARSER_ERRORS(_X)                      \
    _X(OK, NULL) /* = 0 in enum */                   \
    _X(ERR_UNKNOWN, "unknown")                       \
    _X(ERR_CALLBACK, "callback failed")              \
    _X(ERR_INVALID_TYPE, "invalid type character")   \
    _X(ERR_INVALID_INT, "invalid integer character") \
    _X(ERR_OVERFLOW, "overflow")                     \
    _X(ERR_EXPECTED_CR, "expected \\r")              \
    _X(ERR_EXPECTED_LF, "expected \\n")              \

#define _REDIS_PARSER_ERRNO_ENUM_GEN(code, description) REDIS_PARSER_##code,
typedef enum redis_parser_err_e {
    REDIS_PARSER_ERRORS(_REDIS_PARSER_ERRNO_ENUM_GEN)
} redis_parser_err_t;
#undef _REDIS_PARSER_ERRNO_ENUM_GEN

struct redis_protocol_s {
    unsigned char type; /* payload type */
    const redis_protocol_t* parent; /* when nested, parent object */
    int remaining; /* remaining bulk bytes/nested objects */
    int cursor; /* number of processed bulk bytes/nested objects */
    void *data; /* user data */
    size_t poff; /* protocol offset */
    size_t plen; /* protocol length */
    size_t coff; /* content offset */
    size_t clen; /* content length */
};

struct redis_parser_s {
    /* private: callbacks */
    const redis_parser_callbacks_t *callbacks;

    /* private: number of consumed bytes for a single message */
    size_t nread;

    /* private: protocol_t stack (multi-bulk, nested multi-bulk) */
    redis_protocol_t stack[3];
    int stackidx;

    /* private: parser state */
    unsigned char state;
    redis_parser_err_t err;

    /* private: temporary integer (integer reply, bulk length) */
    struct redis_parser_int64_s {
        uint64_t ui64; /* accumulator */
        int64_t i64; /* result */
    } i64;
};

void redis_parser_init(redis_parser_t *parser, const redis_parser_callbacks_t *callbacks);
size_t redis_parser_execute(redis_parser_t *parser, redis_protocol_t **dst, const char *buf, size_t len);
redis_protocol_t *redis_parser_root(redis_parser_t *parser);
redis_parser_err_t redis_parser_err(redis_parser_t *parser);
const char *redis_parser_strerror(redis_parser_err_t err);

#endif // __REDIS_PARSER_H
