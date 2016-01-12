/* SDS (Simple Dynamic Strings), A C dynamic strings library.
 *
 * Copyright (c) 2006-2014, Salvatore Sanfilippo <antirez at gmail dot com>
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

#ifndef __SDS_H
#define __SDS_H

#define SDS_OK 0
#define SDS_ERR_LEN 1
#define SDS_ERR_OOM 2
#define SDS_ERR_OTHER 3
#define SDS_ERR -1

#define SDS_MAX_PREALLOC (1024*1024)

#include <sys/types.h>
#include <stdarg.h>
#ifdef _MSC_VER
#include "win32.h"
#endif

#include <limits.h>
/* this had better match type of sdshdr.len */
#define SDS_MAX_LEN INT_MAX

typedef char *sds;

struct sdshdr {
    int len;
    int free;
    char buf[];
};

static inline size_t sdslen(const sds s) {
    struct sdshdr *sh = (struct sdshdr *)(s-sizeof *sh);
    return sh->len;
}

static inline size_t sdsavail(const sds s) {
    struct sdshdr *sh = (struct sdshdr *)(s-sizeof *sh);
    return sh->free;
}

sds sdsnewlen(const void *init, size_t initlen, int *status);
sds sdsnew(const char *init, int *status);
sds sdsempty(void);
size_t sdslen(const sds s);
sds sdsdup(const sds s);
void sdsfree(sds s);
size_t sdsavail(const sds s);
sds sdsgrowzero(sds s, size_t len, int *status);
sds sdscatlen(sds s, const void *t, size_t len, int *status);
sds sdscat(sds s, const char *t, int *status);
sds sdscatsds(sds s, const sds t, int *status);
sds sdscpylen(sds s, const char *t, size_t len, int *status);
sds sdscpy(sds s, const char *t, int *status);

sds sdscatvprintf(sds s, int *status, const char *fmt, va_list ap);
#ifdef __GNUC__
sds sdscatprintf(sds s, int *status, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
#else
sds sdscatprintf(sds s, int *status, const char *fmt, ...);
#endif

sds sdscatfmt(sds s, int *status, char const *fmt, ...);
void sdstrim(sds s, const char *cset);
void sdsrange(sds s, int start, int end);
void sdsupdatelen(sds s, int *status);
void sdsclear(sds s);
int sdscmp(const sds s1, const sds s2);
sds *sdssplitlen(const char *s, int len, const char *sep, int seplen, int *count, int *status);
void sdsfreesplitres(sds *tokens, int count);
void sdstolower(sds s);
void sdstoupper(sds s);
sds sdsfromlonglong(long long value, int *status);
sds sdscatrepr(sds s, const char *p, size_t len, int *status);
sds *sdssplitargs(const char *line, int *argc, int *status);
sds sdsmapchars(sds s, const char *from, const char *to, size_t setlen);
sds sdsjoin(char **argv, int argc, char *sep, size_t seplen, int *status);
sds sdsjoinsds(sds *argv, int argc, const char *sep, size_t seplen, int *status);

/* Low level functions exposed to the user API */
sds sdsMakeRoomFor(sds s, size_t addlen, int *status);
void sdsIncrLen(sds s, int incr);
sds sdsRemoveFreeSpace(sds s);
size_t sdsAllocSize(sds s);

#endif
