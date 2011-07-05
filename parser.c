#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include "parser.h"

#define CALLBACK(X, args...) do {                                      \
    if (callbacks && callbacks->on_##X) {                              \
        if (callbacks->on_##X(parser, cur , ## args) == 0) {           \
            return pos-buf;                                            \
        }                                                              \
    }                                                                  \
} while(0)

#define RESET_PROTOCOL_T(ptr) do {                                     \
    redis_protocol_t *__tmp = (ptr);                                   \
    __tmp->poff = 0;                                                   \
    __tmp->plen = 0;                                                   \
    __tmp->coff = 0;                                                   \
    __tmp->clen = 0;                                                   \
    __tmp->type = 0;                                                   \
    __tmp->remaining = -1;                                             \
    __tmp->data = NULL;                                                \
} while(0)

#define PARSER_STATES(X)                                               \
    X(unused) /* = 0 in enum */                                        \
    X(type_char)                                                       \
    X(integer_start)                                                   \
    X(integer_19)                                                      \
    X(integer_09)                                                      \
    X(integer_cr)                                                      \
    X(integer_lf)                                                      \
    X(bulk)                                                            \
    X(bulk_cr)                                                         \
    X(bulk_lf)                                                         \
    X(line)                                                            \
    X(line_lf)                                                         \

#define _ENUM_GEN(name) s_##name,
enum state {
    PARSER_STATES(_ENUM_GEN)
};
#undef _ENUM_GEN

#define _ENUM_GEN(s) #s,
static const char * strstate[] = {
    PARSER_STATES(_ENUM_GEN)
};
#undef _ENUM_GEN

#define TRANSITION(st) do {                      \
    pos++; nread++;                              \
    TRANSITION_WITHOUT_POS_INCR(st);             \
} while(0)

#define TRANSITION_WITHOUT_POS_INCR(st) do {     \
    state = s_##st;                              \
    if (pos < end) {                             \
        ch = *pos;                               \
        goto l_##st;                             \
    }                                            \
                                                 \
    /* No more data */                           \
    goto finalize;                               \
} while(0)

#ifdef DEBUG
#define LOG(fmt, args...) do {           \
    fprintf(stderr, fmt "\n" , ## args); \
    fflush(stderr);                      \
} while(0)

#include <ctype.h>

/* Can hold 10 char representations per LOG call */
static char _chrtos_buf[10][8];
static int _chrtos_idx = 0;

static const char *chrtos(char byte) {
    char *buf = _chrtos_buf[_chrtos_idx++ %
        (sizeof(_chrtos_buf) / sizeof(_chrtos_buf[0]))];

    switch(byte) {
    case '\\':
    case '"':
        sprintf(buf,"\\%c",byte);
        break;
    case '\n': sprintf(buf,"\\n"); break;
    case '\r': sprintf(buf,"\\r"); break;
    case '\t': sprintf(buf,"\\t"); break;
    case '\a': sprintf(buf,"\\a"); break;
    case '\b': sprintf(buf,"\\b"); break;
    default:
        if (isprint(byte))
            sprintf(buf,"%c",byte);
        else
            sprintf(buf,"\\x%02x",(unsigned char)byte);
        break;
    }

    return buf;
}
#else
#define LOG(fmt, args...) do { ; } while (0)
#endif

void redis_parser_init(redis_parser_t *parser, const redis_parser_cb_t *callbacks) {
    parser->stackidx = -1;
    parser->callbacks = callbacks;
}

/* Execute the parser against len bytes in buf. When a full message was read,
 * the "dst" pointer is populated with the address of the root object (this
 * address is a static offset in the redis_parser_t struct, but may change in
 * the future). This pointer is set to NULL when no full message could be
 * parsed. This function returns the number of bytes that could be parsed. When
 * no full message was parsed and the return value is smaller than the number
 * of bytes that were available, an error occured and the parser should be
 * re-initialized before parsing more data. */
size_t redis_parser_execute(redis_parser_t *parser, redis_protocol_t **dst, const char *buf, size_t len) {
    redis_protocol_t *stack = parser->stack;
    const redis_parser_cb_t *callbacks = parser->callbacks;
    const char *pos;
    const char *end;
    size_t nread;
    int stackidx;
    unsigned char state;
    struct redis_parser_int64_s i64;
    redis_protocol_t *cur;
    char ch;

    /* Reset destination */
    if (dst) *dst = NULL;

    /* Reset root protocol object for new messages */
    if (parser->stackidx == -1) {
        RESET_PROTOCOL_T(&stack[0]);
        parser->nread = 0;
        parser->stackidx = 0;
        parser->state = s_type_char;
    }

    pos = buf;
    end = buf+len;

    nread = parser->nread;
    stackidx = parser->stackidx;
    state = parser->state;
    i64 = parser->i64;

    while (pos < end && stackidx >= 0) {
        cur = &stack[stackidx];
        cur->parent = stackidx > 0 ? &stack[stackidx-1] : NULL;
        ch = *pos;

        switch (state) {
            case s_type_char:
            l_type_char:
            {
                cur->poff = nread;

                switch (ch) {
                case '$':
                    cur->type = REDIS_STRING_T;
                    TRANSITION(integer_start);
                case '*':
                    cur->type = REDIS_ARRAY_T;
                    TRANSITION(integer_start);
                case ':':
                    cur->type = REDIS_INTEGER_T;
                    TRANSITION(integer_start);
                case '+':
                    cur->type = REDIS_STATUS_T;
                    assert(NULL);
                case '-':
                    cur->type = REDIS_ERROR_T;
                    assert(NULL);
                }

                goto error;
            }

            case s_integer_start:
            l_integer_start:
            {
                i64.neg = 0;
                i64.ui64 = 0;

                /* Start with number */
                if (ch >= '1' && ch <= '9') {
                    i64.ui64 = ch - '0';
                    TRANSITION(integer_09);
                }

                /* Start with negative sign */
                if (ch == '-') {
                    i64.neg = 1;
                    TRANSITION(integer_19);
                }

                /* Start with positive sign */
                if (ch == '+') {
                    i64.neg = 0;
                    TRANSITION(integer_19);
                }

                /* Single integer character is a zero */
                if (ch == '0') {
                    TRANSITION(integer_cr);
                }

                goto error;
            }

            case s_integer_19:
            l_integer_19:
            {
                if (ch >= '1' && ch <= '9') {
                    i64.ui64 = ch - '0';
                    TRANSITION(integer_09);
                }

                goto error;
            }

            case s_integer_09:
            l_integer_09:
            {
                if (ch >= '0' && ch <= '9') {
                    if (i64.ui64 > (UINT64_MAX / 10)) /* Overflow */
                        goto error;
                    i64.ui64 *= 10;
                    if (i64.ui64 > (UINT64_MAX - (ch - '0'))) /* Overflow */
                        goto error;
                    i64.ui64 += ch - '0';
                    TRANSITION(integer_09);
                } else if (ch == '\r') {
                    TRANSITION(integer_lf);
                }

                goto error;
            }

            case s_integer_cr:
            l_integer_cr:
            {
                if (ch == '\r') {
                    TRANSITION(integer_lf);
                }

                goto error;
            }

            case s_integer_lf:
            l_integer_lf:
            {
                if (ch != '\n') {
                    goto error;
                }

                /* Check if the uint64_t can be safely casted to int64_t */
                if (i64.neg) {
                    if (i64.ui64 > ((uint64_t)(-(INT64_MIN+1))+1)) /* Overflow */
                        goto error;
                    i64.i64 = -i64.ui64;
                } else {
                    if (i64.ui64 > INT64_MAX) /* Overflow */
                        goto error;
                    i64.i64 = i64.ui64;
                }

                /* Protocol length can be set regardless of type */
                cur->plen = nread - cur->poff + 1; /* include \n */

                if (cur->type == REDIS_STRING_T) {
                    if (i64.i64 < 0) { /* nil bulk */
                        CALLBACK(nil);
                        goto done;
                    }

                    /* Setup content offset and length */
                    cur->coff = nread + 1; /* include \n */
                    cur->clen = (unsigned)i64.i64;
                    cur->plen += cur->clen + 2; /* include \r\n */

                    /* Store remaining bytes for a complete bulk */
                    cur->remaining = (unsigned)i64.i64;
                    TRANSITION(bulk);
                }

                if (cur->type == REDIS_ARRAY_T) {
                    if (i64.i64 < 0) { /* nil multi bulk */
                        CALLBACK(nil);
                        goto done;
                    }

                    /* Store remaining objects for a complete multi bulk */
                    cur->remaining = (unsigned)i64.i64;
                    CALLBACK(array, cur->remaining);
                    goto done;
                }

                if (cur->type == REDIS_INTEGER_T) {
                    /* Setup content offset and length */
                    cur->coff = cur->poff + 1;
                    cur->clen = nread - cur->coff - 1; /* remove \r */
                    CALLBACK(integer, i64.i64);
                    goto done;
                }

                assert(NULL && "unexpected object type in s_integer_lf");
            }

            case s_bulk:
            l_bulk:
            {
                size_t len = cur->remaining;

                /* Everything can be read */
                if (len <= (end-pos)) {
                    cur->remaining -= len;
                    CALLBACK(string, pos, len);
                    pos += len; nread += len;
                    TRANSITION_WITHOUT_POS_INCR(bulk_cr);
                }

                /* Not everything can be read */
                len = (end-pos);
                cur->remaining -= len;
                CALLBACK(string, pos, len);
                pos += len; nread += len;
                goto finalize;
            }

            case s_bulk_cr:
            l_bulk_cr:
            {
                if (ch == '\r') {
                    TRANSITION(bulk_lf);
                }

                goto error;
            }

            case s_bulk_lf:
            l_bulk_lf:
            {
                if (ch == '\n') {
                    goto done;
                }

                goto error;
            }
        }

        /* Transitions should be made from within the switch */
        assert(NULL && "invalid code path");

        done:
            /* Message is done when root object is done */
            while (stackidx >= 0) {
                /* Move to nested object when we see an incomplete array */
                cur = &stack[stackidx];
                if (cur->type == REDIS_ARRAY_T && cur->remaining) {
                    RESET_PROTOCOL_T(&stack[++stackidx]);
                    cur->remaining--;
                    break;
                }

                /* Aggregate plen for nested objects */
                if (stackidx > 0) {
                    stack[stackidx-1].plen += cur->plen;
                }

                stackidx--;
            }

            /* Always move back to start state */
            state = s_type_char;
            pos++; nread++;
            continue;
    }

    /* Set destination pointer when full message was read */
    if (stackidx == -1) {
        if (dst) *dst = &stack[0];
    }

finalize:

    parser->nread = nread;
    parser->stackidx = stackidx;
    parser->state = state;
    parser->i64 = i64;
    return pos-buf;

error:

    /* TODO: set some kind of error */
    goto finalize;
}
