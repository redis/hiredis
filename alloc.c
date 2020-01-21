#include "fmacros.h"
#include "alloc.h"
#include <string.h>

void *hiredis_safe_malloc(size_t size) {
    void *ptr = malloc(size);
    if (ptr == NULL)
        HIREDIS_OOM_HANDLER;

    return ptr;
}

void *hiredis_safe_calloc(size_t nmemb, size_t size) {
    void *ptr = calloc(nmemb, size);
    if (ptr == NULL)
        HIREDIS_OOM_HANDLER;

    return ptr;
}

void *hiredis_safe_realloc(void *ptr, size_t size) {
    void *newptr = realloc(ptr, size);
    if (newptr == NULL)
        HIREDIS_OOM_HANDLER;

    return newptr;
}

char *hiredis_safe_strdup(const char *str) {
    char *newstr = strdup(str);
    if (newstr == NULL)
        HIREDIS_OOM_HANDLER;

    return newstr;
}
