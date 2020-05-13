/*
 * Copyright (c) 2020, Michael Grunder <michael dot grunder at gmail dot com>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef HIREDIS_ALLOC_H
#define HIREDIS_ALLOC_H

#include <stddef.h> /* for size_t */

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _WIN32

/* Hiredis allocation/deallocation function pointers */
typedef void*(*hiredisMallocFn)(size_t);
typedef void*(*hiredisCallocFn)(size_t,size_t);
typedef void*(*hiredisReallocFn)(void*,size_t);
typedef char*(*hiredisStrdupFn)(const char*);
typedef void (*hiredisFreeFn)(void*);

/* Structure pointing to our actually configured allocators */
typedef struct hiredisAllocators {
    hiredisMallocFn malloc;
    hiredisCallocFn calloc;
    hiredisReallocFn realloc;
    hiredisStrdupFn strdup;
    hiredisFreeFn free;
} hiredisAllocators;

/* Hiredis' configured allocator function pointer struct */
extern hiredisAllocators hiredisAllocFns;

static inline void *hi_malloc(size_t size) {
    return hiredisAllocFns.malloc(size);
}

static inline void *hi_calloc(size_t nmemb, size_t size) {
    return hiredisAllocFns.calloc(nmemb, size);
}

static inline void *hi_realloc(void *ptr, size_t size) {
    return hiredisAllocFns.realloc(ptr, size);
}

static inline char *hi_strdup(const char *str) {
    return hiredisAllocFns.strdup(str);
}

static inline void hi_free(void *ptr) {
    hiredisAllocFns.free(ptr);
}

hiredisAllocators hiredisSetAllocators(hiredisAllocators *ha);
void hiredisResetAllocators(void);

#else

#include <stdlib.h>
#include <string.h>

/* Just point to system allocators in Windows */
#define hi_malloc malloc
#define hi_calloc calloc
#define hi_realloc realloc
#define hi_strdup strdup
#define hi_free free

#ifdef __cplusplus
}
#endif

#endif
#endif /* HIREDIS_ALLOC_H */
