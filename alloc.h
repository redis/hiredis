#ifndef ALLOC_H

#include <stdlib.h>

#ifndef HIREDIS_OOM_HANDLER
#define HIREDIS_OOM_HANDLER abort()
#endif

void *hiredis_safe_malloc(size_t size);
void *hiredis_safe_calloc(size_t nmemb, size_t size);
void *hiredis_safe_realloc(void *ptr, size_t size);
char *hiredis_safe_strdup(const char *str);

#endif  /* ALLOC_H */
