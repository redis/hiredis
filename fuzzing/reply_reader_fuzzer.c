/*
 * Copyright (c) 2026, Redis Ltd.
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

/* Fuzzes the RESP reply reader, which is the one part of hiredis that consumes
 * bytes an attacker may control: everything a server sends arrives here before
 * any caller sees it.
 *
 * The first input byte seeds a deterministic sequence of feed sizes so that
 * replies are delivered in varied fragments, exercising the incremental
 * parser rather than only the case where a whole reply lands in one read(). */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "hiredis.h"
#include "read.h"

/* Bound the multi-bulk element count. Without this a header such as
 * "*4294967295\r\n" makes the reader ask the allocator for tens of gigabytes,
 * which reports as an out-of-memory in the fuzzer rather than as a defect in
 * the parser. Callers bound this with redisReader->maxelements; the fuzzer
 * does the same so that findings are parser bugs. */
#define FUZZ_MAX_ELEMENTS 1024

/* Marsaglia's xorshift32. The state must be nonzero. */
static uint32_t fuzzRandom(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *state = x;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    redisReader *reader;
    uint32_t state;
    size_t offset = 0;
    void *reply;

    if (size < 2)
        return 0;

    state = (uint32_t)data[0] + 1;
    data++;
    size--;

    reader = redisReaderCreate();
    if (reader == NULL)
        return 0;

    reader->maxelements = FUZZ_MAX_ELEMENTS;

    while (offset < size) {
        size_t chunk = 1 + (fuzzRandom(&state) % 256);
        size_t len = size - offset;
        if (len > chunk)
            len = chunk;

        if (redisReaderFeed(reader, (const char *)data + offset, len) != REDIS_OK)
            break;
        offset += len;

        while (redisReaderGetReply(reader, &reply) == REDIS_OK && reply != NULL) {
            freeReplyObject(reply);
            reply = NULL;
        }
    }

    redisReaderFree(reader);
    return 0;
}
