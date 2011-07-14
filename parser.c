#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include "parser.h"

#define ERRNO(code) REDIS_PARSER_##code
#define SET_ERRNO(code) do {                                           \
    parser->errno = ERRNO(code);                                       \
} while(0)

/* The redis_protocol_t argument to the callback function must be provided by
 * the caller because "ISO C99 requires rest arguments to be used". */
#define CALLBACK(X, ...) do {                                          \
    if (callbacks && callbacks->on_##X) {                              \
        if (callbacks->on_##X(parser, __VA_ARGS__) != 0) {             \
            SET_ERRNO(ERR_CALLBACK);                                   \
            goto error;                                                \
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
    __tmp->cursor = 0;                                                 \
    __tmp->data = NULL;                                                \
} while(0)

#define REDIS_PARSER_STATES(_X)                                        \
    _X(unused) /* = 0 in enum */                                       \
    _X(type_char)                                                      \
    _X(integer_start)                                                  \
    _X(integer_pos_19)                                                 \
    _X(integer_pos_09)                                                 \
    _X(integer_neg_19)                                                 \
    _X(integer_neg_09)                                                 \
    _X(integer_cr)                                                     \
    _X(integer_lf)                                                     \
    _X(bulk)                                                           \
    _X(bulk_cr)                                                        \
    _X(bulk_lf)                                                        \
    _X(line)                                                           \
    _X(line_lf)                                                        \

#define _GEN(name) s_##name,
enum state {
    REDIS_PARSER_STATES(_GEN)
};
#undef _GEN

#define _GEN(code, description) description,
static const char *errno_str[] = {
    REDIS_PARSER_ERRORS(_GEN)
};
#undef _GEN

#define STATE(st)                                                      \
    case s_##st:                                                       \
    l_##st:                                                            \
    state = s_##st;                                                    \
    if (pos >= end) { /* No more data */                               \
        goto finalize;                                                 \
    }

#define ADVANCE(bytes) do {                                            \
    pos += (bytes); nread += (bytes);                                  \
} while(0)

#define MOVE(st) do {                                                  \
    goto l_##st;                                                       \
} while(0)

#define ADVANCE_AND_MOVE(st) do {                                      \
    ADVANCE(1);                                                        \
    MOVE(st);                                                          \
} while(0)

#ifdef DEBUG
#define _ENUM_GEN(s) #s,
static const char *state_str[] = {
    REDIS_PARSER_STATES(_ENUM_GEN)
};
#undef _ENUM_GEN

#define LOG(fmt, ...) do {                                             \
    fprintf(stderr, fmt "\n", __VA_ARGS__);                            \
    fflush(stderr);                                                    \
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
#define LOG(fmt, ...) do { ; } while (0)
#endif

void redis_parser_init(redis_parser_t *parser, const redis_parser_callbacks_t *callbacks) {
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
    const redis_parser_callbacks_t *callbacks = parser->callbacks;
    const char *pos;
    const char *end;
    size_t nread;
    int stackidx;
    unsigned char state;
    struct redis_parser_int64_s i64;
    redis_protocol_t *cur;

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

        switch (state) {
            STATE(type_char) {
                cur->poff = nread;

                switch (*pos) {
                case '$':
                    cur->type = REDIS_STRING_T;
                    ADVANCE_AND_MOVE(integer_start);
                case '*':
                    cur->type = REDIS_ARRAY_T;
                    ADVANCE_AND_MOVE(integer_start);
                case ':':
                    cur->type = REDIS_INTEGER_T;
                    ADVANCE_AND_MOVE(integer_start);
                case '+':
                    cur->type = REDIS_STATUS_T;
                    ADVANCE_AND_MOVE(line);
                case '-':
                    cur->type = REDIS_ERROR_T;
                    ADVANCE_AND_MOVE(line);
                }

                SET_ERRNO(ERR_INVALID_TYPE);
                goto error;
            }

            STATE(integer_start) {
                char ch = *pos;
                i64.i64 = 0;
                i64.ui64 = 0;

                /* Start unsigned, thus positive */
                if (ch >= '1' && ch <= '9') {
                    i64.ui64 = ch - '0';
                    ADVANCE_AND_MOVE(integer_pos_09);
                }

                /* Start with negative sign */
                if (ch == '-') {
                    ADVANCE_AND_MOVE(integer_neg_19);
                }

                /* Start with positive sign */
                if (ch == '+') {
                    ADVANCE_AND_MOVE(integer_pos_19);
                }

                /* Single integer character is a zero */
                if (ch == '0') {
                    ADVANCE_AND_MOVE(integer_cr);
                }

                SET_ERRNO(ERR_INVALID_INT);
                goto error;
            }

            STATE(integer_pos_19) {
                char ch = *pos;

                if (ch >= '1' && ch <= '9') {
                    i64.ui64 = ch - '0';
                    ADVANCE_AND_MOVE(integer_pos_09);
                }

                SET_ERRNO(ERR_INVALID_INT);
                goto error;
            }

            STATE(integer_pos_09) {
                char ch = *pos;

                if (ch >= '0' && ch <= '9') {
                    if (i64.ui64 > ((uint64_t)INT64_MAX / 10)) { /* Overflow */
                        SET_ERRNO(ERR_OVERFLOW);
                        goto error;
                    }
                    i64.ui64 *= 10;
                    if (i64.ui64 > ((uint64_t)INT64_MAX - (ch - '0'))) { /* Overflow */
                        SET_ERRNO(ERR_OVERFLOW);
                        goto error;
                    }
                    i64.ui64 += ch - '0';
                    ADVANCE_AND_MOVE(integer_pos_09);
                } else if (ch == '\r') {
                    i64.i64 = i64.ui64;
                    ADVANCE_AND_MOVE(integer_lf);
                }

                SET_ERRNO(ERR_INVALID_INT);
                goto error;
            }

            STATE(integer_neg_19) {
                char ch = *pos;

                if (ch >= '1' && ch <= '9') {
                    i64.ui64 = ch - '0';
                    ADVANCE_AND_MOVE(integer_neg_09);
                }

                SET_ERRNO(ERR_INVALID_INT);
                goto error;
            }

            STATE(integer_neg_09) {
                char ch = *pos;

                if (ch >= '0' && ch <= '9') {
                    if (i64.ui64 > (((uint64_t)-(INT64_MIN+1)+1) / 10)) { /* Overflow */
                        SET_ERRNO(ERR_OVERFLOW);
                        goto error;
                    }
                    i64.ui64 *= 10;
                    if (i64.ui64 > (((uint64_t)-(INT64_MIN+1)+1) - (ch - '0'))) { /* Overflow */
                        SET_ERRNO(ERR_OVERFLOW);
                        goto error;
                    }
                    i64.ui64 += ch - '0';
                    ADVANCE_AND_MOVE(integer_neg_09);
                } else if (ch == '\r') {
                    i64.i64 = -i64.ui64;
                    ADVANCE_AND_MOVE(integer_lf);
                }

                SET_ERRNO(ERR_INVALID_INT);
                goto error;
            }

            STATE(integer_cr) {
                if (*pos == '\r') {
                    ADVANCE_AND_MOVE(integer_lf);
                }

                SET_ERRNO(ERR_EXPECTED_CR);
                goto error;
            }

            STATE(integer_lf) {
                if (*pos != '\n') {
                    SET_ERRNO(ERR_EXPECTED_LF);
                    goto error;
                }

                /* Protocol length can be set regardless of type */
                cur->plen = nread - cur->poff + 1; /* include \n */

                if (cur->type == REDIS_STRING_T) {
                    if (i64.i64 < 0) { /* nil bulk */
                        CALLBACK(nil, cur);
                        goto done;
                    }

                    /* Setup content offset and length */
                    cur->coff = nread + 1; /* include \n */
                    cur->clen = (unsigned)i64.i64;
                    cur->plen += cur->clen + 2; /* include \r\n */

                    /* Store remaining bytes for a complete bulk */
                    cur->remaining = (unsigned)i64.i64;
                    ADVANCE_AND_MOVE(bulk);
                }

                if (cur->type == REDIS_ARRAY_T) {
                    if (i64.i64 < 0) { /* nil multi bulk */
                        CALLBACK(nil, cur);
                        goto done;
                    }

                    /* Store remaining objects for a complete multi bulk */
                    cur->remaining = (unsigned)i64.i64;
                    cur->cursor = -1; /* Is incremented in "done" */
                    CALLBACK(array, cur, cur->remaining);
                    goto done;
                }

                if (cur->type == REDIS_INTEGER_T) {
                    /* Setup content offset and length */
                    cur->coff = cur->poff + 1;
                    cur->clen = nread - cur->coff - 1; /* remove \r */
                    CALLBACK(integer, cur, i64.i64);
                    goto done;
                }

                assert(NULL && "unexpected object type in s_integer_lf");
            }

            STATE(bulk) {
                size_t remaining = cur->remaining;
                size_t available = (end-pos);

                /* Everything can be read */
                if (remaining <= available) {
                    cur->remaining = 0;
                    CALLBACK(string, cur, pos, remaining);
                    ADVANCE(remaining);
                    MOVE(bulk_cr);
                }

                /* Not everything can be read */
                cur->remaining -= available;
                CALLBACK(string, cur, pos, available);
                ADVANCE(available);

                /* Add number of processed bytes to cursor */
                cur->cursor += available;
                goto finalize;
            }

            STATE(bulk_cr) {
                if (*pos == '\r') {
                    ADVANCE_AND_MOVE(bulk_lf);
                }

                SET_ERRNO(ERR_EXPECTED_CR);
                goto error;
            }

            STATE(bulk_lf) {
                if (*pos == '\n') {
                    goto done;
                }

                SET_ERRNO(ERR_EXPECTED_LF);
                goto error;
            }

            STATE(line) {
                const char *mark = pos;

                /* Remove tight loop and add function-wide "line mark" once
                 * limits on line length are added. */
                while(pos < end) {
                    if (*pos == '\r') {
                        cur->coff = cur->poff + 1;
                        cur->clen = nread - cur->coff;
                        CALLBACK(string, cur, mark, pos-mark);
                        ADVANCE_AND_MOVE(line_lf);
                    }

                    ADVANCE(1);
                }

                /* No more data */
                CALLBACK(string, cur, mark, pos-mark);

                /* Add number of processed bytes to cursor */
                cur->cursor += pos-mark;
                goto finalize;
            }

            STATE(line_lf) {
                if (*pos == '\n') {
                    cur->plen = nread - cur->poff + 1; /* include \n */
                    goto done;
                }

                SET_ERRNO(ERR_EXPECTED_LF);
                goto error;
            }
        }

        /* Transitions should be made from within the switch */
        assert(NULL && "invalid code path");

    done:
        /* Message is done when root object is done */
        do {
            /* Move to nested object when we see an incomplete array */
            if (cur->type == REDIS_ARRAY_T && cur->remaining) {
                RESET_PROTOCOL_T(&stack[++stackidx]);
                cur->cursor++;
                cur->remaining--;
                break;
            }

            /* Aggregate plen for nested objects */
            if (stackidx > 0) {
                stack[stackidx-1].plen += cur->plen;
            }

            cur = &stack[--stackidx];
        } while (stackidx >= 0);

        /* Always move back to start state */
        state = s_type_char;
        ADVANCE(1);
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

    if (parser->errno == ERRNO(OK)) {
        SET_ERRNO(ERR_UNKNOWN);
    }

    return pos-buf;
}

redis_parser_errno_t redis_parser_errno(redis_parser_t *parser) {
    return parser->errno;
}

const char *redis_parser_strerror(redis_parser_errno_t errno) {
    if (errno < (sizeof(errno_str)/sizeof(errno_str[0])))
        return errno_str[errno];
    return NULL;
}
