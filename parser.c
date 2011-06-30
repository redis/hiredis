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
    X(integer_sign)                                                    \
    X(integer_start)                                                   \
    X(integer_body)                                                    \
    X(integer_lf)                                                      \
    X(bulk)                                                            \
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
 * the "dst" pointer is populated with the address of the root object ( this
 * address is a static offset in the redis_parser_t struct, but may change in
 * the future). This pointer is set to NULL when no full message could be
 * parsed. This function returns the number of bytes that could be parsed. When
 * no full message was parsed and the return value is smaller than the number
 * of bytes that were available, an error occured and the parser should be
 * re-initialized before parsing more data. */
size_t redis_parser_execute(redis_parser_t *parser, const char *buf, size_t len, redis_protocol_t *dst) {
    redis_protocol_t *stack = parser->stack;
    const redis_parser_cb_t *callbacks = parser->callbacks;
    const char *pos;
    const char *end;
    size_t nread;
    int stackidx;
    unsigned char state;

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

    while (pos < end && stackidx >= 0) {
        redis_protocol_t *cur = &stack[stackidx];
        cur->parent = stackidx > 0 ? &stack[stackidx-1] : NULL;

        while (pos < end) {
            char ch = *pos;

            LOG("state: %-18s char: %4s", strstate[state], chrtos(ch));

            switch (state) {
            case s_type_char:
            {
                switch (ch) {
                case '$':
                    cur->type = REDIS_STRING_T;
                    state = s_integer_sign;
                    break;
                case '*':
                    cur->type = REDIS_ARRAY_T;
                    state = s_integer_sign;
                    break;
                case ':':
                    cur->type = REDIS_INTEGER_T;
                    state = s_integer_sign;
                    break;
                case '+':
                    cur->type = REDIS_STATUS_T;
                    state = s_line;
                    break;
                case '-':
                    cur->type = REDIS_ERROR_T;
                    state = s_line;
                    break;
                default:
                    goto error;
                }
                break;
            }

            case s_integer_sign:
            {
                parser->i64.neg = (ch == '-');
                parser->i64.ui64 = 0;
                state = s_integer_start;

                /* Break when char was consumed */
                if (ch == '-' || ch == '+')
                    break;

                /* Char was not consumed, jump to s_integer_start */
                goto l_integer_start;
            }

            case s_integer_start:
            l_integer_start:
            {
                if (ch >= '1' && ch <= '9') {
                    parser->i64.ui64 = ch - '0';
                    state = s_integer_body;
                } else {
                    goto error;
                }
                break;
            }

            case s_integer_body:
            {
                if (ch >= '0' && ch <= '9') {
                    parser->i64.ui64 *= 10;
                    parser->i64.ui64 += ch - '0';
                } else if (ch == '\r') {
                    state = s_integer_lf;
                } else {
                    goto error;
                }
                break;
            }

            case s_integer_lf:
            {
                int64_t i64;

                if (ch != '\n') {
                    goto error;
                }

                i64 = parser->i64.ui64 & INT64_MAX;
                if (parser->i64.neg)
                    i64 *= -1;

                /* Protocol length can be set regardless of type */
                cur->plen = nread - cur->poff + 1; /* include \n */

                /* This should be done in the state machine itself, but I don't
                 * want to dup the integer states for these three types. */
                switch (cur->type) {
                case REDIS_STRING_T:  goto l_integer_lf_string_t;
                case REDIS_ARRAY_T:   goto l_integer_lf_array_t;
                case REDIS_INTEGER_T: goto l_integer_lf_integer_t;
                default:
                    assert(NULL);
                    goto error;
                }

            l_integer_lf_string_t:
                /* Trap the nil bulk */
                if (i64 < 0) {
                    CALLBACK(nil);
                    goto done;
                }

                /* Setup content offset length. Note that the content length is
                 * known upfront, but not necessarily valid (we may see EOF
                 * before seeing the last content byte). */
                cur->coff = nread + 1; /* include \n */
                cur->clen = (unsigned)i64;
                cur->plen += cur->clen + 2; /* include \r\n */

                /* Store remaining bytes for a complete bulk */
                cur->remaining = (unsigned)i64;
                state = s_bulk;
                break;

            l_integer_lf_array_t:
                /* Trap the nil multi bulk */
                if (i64 < 0) {
                    CALLBACK(nil);
                    goto done;
                }

                cur->remaining = (unsigned)i64;
                CALLBACK(array, cur->remaining);
                goto done;

            l_integer_lf_integer_t:
                /* Any integer is OK */
                CALLBACK(integer, i64);
                goto done;
            }

            case s_bulk:
            {
                size_t len = cur->remaining;

                if (len) {
                    if ((end-pos) < len) len = (end-pos);
                    CALLBACK(string, pos, len);
                    cur->remaining -= len;
                    pos += len - 1; nread += len - 1;
                    break;
                }

                /* No remaining bytes for this bulk */
                if (ch != '\r') {
                    goto error;
                }

                state = s_bulk_lf;
                break;
            }

            case s_bulk_lf:
            {
                if (ch != '\n') {
                    goto error;
                }
                goto done;
            }

            default:
                assert(NULL);
            }

            pos++; nread++;
            continue;

        done:
            /* Message is done when root object is done */
            while (stackidx >= 0) {
                /* Move to nested object when we see an incomplete array */
                cur = &stack[stackidx];
                if (cur->type == REDIS_ARRAY_T && cur->remaining) {
                    RESET_PROTOCOL_T(&stack[++stackidx]);
                    break;
                }
                stackidx--;
            }

            /* When an object is done, always move back to start state */
            state = s_type_char;

            pos++; nread++;
            break;
        }
    }

error:

    parser->nread = nread;
    parser->stackidx = stackidx;
    parser->state = state;
    return pos-buf;
}
