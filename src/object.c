#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "object.h"

void redis_object_free(redis_object **ptr) {
    redis_object *obj = *ptr;

    switch(obj->type) {
    case REDIS_STRING:
    case REDIS_STATUS:
    case REDIS_ERROR:
        if (obj->str != NULL) {
            free(obj->str);
            obj->str = NULL;
            obj->len = 0;
        }
        break;
    case REDIS_ARRAY:
        if (obj->element != NULL) {
            unsigned int j;
            for (j = 0; j < obj->elements; j++) {
                if (obj->element[j] != NULL) {
                    redis_object_free(&obj->element[j]);
                }
            }
            free(obj->element);
            obj->element = NULL;
            obj->elements = 0;
        }
        break;
    }

    free(*ptr);
    *ptr = NULL;
}

static redis_object *_object_create_from_protocol(redis_protocol *p) {
    redis_object *obj, *parent;

    /* Create object when the callback was not fired before. */
    if (p->data == NULL) {
        obj = p->data = malloc(sizeof(redis_object));
        if (obj == NULL) {
            return NULL;
        }

        obj->type = p->type;

        obj->str = NULL;
        obj->len = 0;

        obj->element = NULL;
        obj->elements = 0;

        if (p->parent) {
            parent = (redis_object*)p->parent->data;
            assert(parent->type == REDIS_ARRAY);
            parent->element[p->parent->cursor] = obj;
        }
    }

    obj = (redis_object*)p->data;
    assert(obj && obj->type == p->type);

    return obj;
}

static int _object_string_cb(redis_parser *parser, redis_protocol *p, const char *buf, size_t len) {
    redis_object *self;
    char *dst;

    ((void)parser);

    self = _object_create_from_protocol(p);
    if (self == NULL) {
        return -1;
    }

    if (self->type == REDIS_STRING) {
        assert(p->size >= 0);

        dst = self->str;

        /* The size is known upfront: allocate memory */
        if (dst == NULL) {
            dst = malloc(p->size+1);
            if (dst == NULL) {
                return -1;
            }
        }
    } else {
        assert(p->size < 0);

        /* The size is not known upfront: dynamically allocate memory */
        dst = realloc(self->str, self->len + len + 1);
        if (dst == NULL) {
            return -1;
        }
    }

    /* Copy provided buffer */
    memcpy(dst + self->len, buf, len);
    self->str = dst;
    self->len += len;
    self->str[self->len] = '\0';

    return 0;
}

static int _object_array_cb(redis_parser *parser, redis_protocol *p, size_t len) {
    redis_object *self;

    ((void)parser);

    self = _object_create_from_protocol(p);
    if (self == NULL) {
        return -1;
    }

    if (len > 0) {
        self->element = calloc(len, sizeof(redis_object*));
        if (self->element == NULL) {
            return -1;
        }
    }

    self->elements = len;

    return 0;
}

static int _object_integer_cb(redis_parser *parser, redis_protocol *p, int64_t value) {
    redis_object *self;

    ((void)parser);

    self = _object_create_from_protocol(p);
    if (self == NULL) {
        return -1;
    }

    self->integer = value;

    return 0;
}

static int _object_nil_cb(redis_parser *parser, redis_protocol *p) {
    redis_object *self;

    ((void)parser);

    self = _object_create_from_protocol(p);
    if (self == NULL) {
        return -1;
    }

    return 0;
}

static void _object_destroy_cb(redis_parser *parser, redis_protocol *p) {
    redis_object *self;

    ((void)parser);

    self = p->data;
    redis_object_free(&self);
}

/* Set of callbacks that can be passed to the parser. */
redis_parser_callbacks redis_object_parser_callbacks = {
    .on_string = _object_string_cb,
    .on_array = _object_array_cb,
    .on_integer = _object_integer_cb,
    .on_nil = _object_nil_cb,
    .destroy = _object_destroy_cb
};
