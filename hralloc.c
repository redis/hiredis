#include "fmacros.h"
#include "hralloc.h"
#include <string.h>

void *hr_safe_malloc(size_t size) {
    void *ptr = malloc(size);
    if (ptr == NULL)
        HR_OOM_HANDLER;

    return ptr;
}

void *hr_safe_calloc(size_t nmemb, size_t size) {
    void *ptr = calloc(nmemb, size);
    if (ptr == NULL)
        HR_OOM_HANDLER;

    return ptr;
}

void *hr_safe_realloc(void *ptr, size_t size) {
    void *newptr = realloc(ptr, size);
    if (newptr == NULL)
        HR_OOM_HANDLER;

    return newptr;
}

char *hr_safe_strdup(const char *str) {
    char *newstr = strdup(str);
    if (newstr == NULL)
        HR_OOM_HANDLER;

    return newstr;
}
