#ifndef __HRALLOC_H

#include <stdlib.h>

#ifndef HR_OOM_HANDLER
#define HR_OOM_HANDLER abort()
#endif

void *hr_safe_malloc(size_t size);
void *hr_safe_calloc(size_t nmemb, size_t size);
void *hr_safe_realloc(void *ptr, size_t size);
char *hr_safe_strdup(const char *str);

#endif  /* __HRALLOC_H */
