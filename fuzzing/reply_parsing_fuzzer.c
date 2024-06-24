/*
 * Copyright (c) 2024, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2024, Pieter Noordhuis <pcnoordhuis at gmail dot com>
 * Copyright (c) 2024, Matt Stancliff <matt at genges dot com>,
 *                     Jan-Erik Rediger <janerik at fnordig dot com>
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

#include "hiredis.h"
#include <stdlib.h>
#include <string.h>

#define MAX_MALLOC_BYTES (10 * 1024 * 1024)

static void *fuzz_malloc(size_t size) {
    if (size > MAX_MALLOC_BYTES) {
        return NULL;
    }

    return malloc(size);
}

static void *fuzz_calloc(size_t nmemb, size_t size) {
    if (nmemb * size > MAX_MALLOC_BYTES) {
        return NULL;
    }

    return calloc(nmemb, size);
}

static void *fuzz_realloc(void *ptr, size_t size) {
    if (size > MAX_MALLOC_BYTES) {
        return NULL;
    }

    return realloc(ptr, size);
}

int LLVMFuzzerInitialize(int *argc, char ***argv) {
    hiredisAllocFuncs ha = {
        .mallocFn = fuzz_malloc,
        .callocFn = fuzz_calloc,
        .reallocFn = fuzz_realloc,
        .strdupFn = strdup,
        .freeFn = free,
    };

    hiredisSetAllocators(&ha);
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    redisReader *reader = redisReaderCreate();

    if (redisReaderFeed(reader, (const char *)data, size) == REDIS_OK) {
        void *reply;
        redisReaderGetReply(reader, &reply);
        freeReplyObject(reply);
    }

    redisReaderFree(reader);
    return 0;
}
