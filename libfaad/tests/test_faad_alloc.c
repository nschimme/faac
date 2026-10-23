/*
 * FAAD - Freeware Advanced Audio Decoder
 * Copyright (C) 2026 Nils Schimmelmann
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */

#include "faad_internal.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>

void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void __real_free(void *ptr);

static size_t calls, live, total, largest, fail_at;

void *__wrap_malloc(size_t size)
{
    calls++;
    if (calls == fail_at) return NULL;
    void *ptr = __real_malloc(size);
    if (ptr) {
        live++;
        total += size;
        if (size > largest) largest = size;
    }
    return ptr;
}

void *__wrap_calloc(size_t count, size_t size)
{
    calls++;
    if (calls == fail_at) return NULL;
    void *ptr = __real_calloc(count, size);
    if (ptr) {
        live++;
        total += count * size;
        if (count * size > largest) largest = count * size;
    }
    return ptr;
}

void __wrap_free(void *ptr)
{
    if (ptr) live--;
    __real_free(ptr);
}

#ifndef FAAD_DISABLE_SBR
static void test_lazy_allocations(bool ps)
{
    faad_config cfg;
    assert(faad_config_init(&cfg, sizeof(cfg)) == FAAD_OK);
    /* Each failed first-use request must be retryable, including when earlier
     * optional blocks were already allocated successfully. */
    size_t optional_calls = 0;
    for (size_t failure = 0; failure <= optional_calls; failure++) {
        calls = live = total = largest = fail_at = 0;
        faad_decoder *dec = NULL;
        assert(faad_decoder_open(&cfg, NULL, 0, &dec) == FAAD_OK);
        assert(!dec->sbr);
        if (ps) assert(faad_ensure_sbr(dec) == FAAD_OK);
        size_t before = calls;
        if (failure) fail_at = before + failure;
        faad_status status;
#ifndef FAAD_DISABLE_PS
        if (ps) status = faad_ensure_ps(dec);
        else
#endif
            status = faad_ensure_sbr(dec);
        if (!failure) {
            assert(status == FAAD_OK);
            optional_calls = calls - before;
        } else {
            assert(status == FAAD_ERR_NO_MEMORY);
            fail_at = 0;
#ifndef FAAD_DISABLE_PS
            if (ps) assert(faad_ensure_ps(dec) == FAAD_OK);
            else
#endif
                assert(faad_ensure_sbr(dec) == FAAD_OK);
        }
        assert(dec->sbr && dec->sbr_scratch.y);
        assert(dec->allocated_bytes == total);
        assert(faad_decoder_close(&dec) == FAAD_OK && !live);
    }
}
#endif

#if MAX_CHANNELS >= 3 /* mono and stereo builds omit the coupling tool */
static void test_coupling_allocation(void)
{
    faad_config cfg;
    assert(faad_config_init(&cfg, sizeof(cfg)) == FAAD_OK);
    calls = live = total = largest = fail_at = 0;
    faad_decoder *dec = NULL;
    assert(faad_decoder_open(&cfg, NULL, 0, &dec) == FAAD_OK);
    assert(!dec->coupling);
    const uint8_t zero_cce[8] = {0}; /* tag 0, one SCE target, empty ICS */
    BitReader bs;
    bits_init(&bs, zero_cce, sizeof(zero_cce));
    fail_at = calls + 1;
    assert(decode_cce(&bs, dec) == FAAD_ERR_NO_MEMORY);
    assert(!dec->coupling);
    fail_at = 0;
    bits_init(&bs, zero_cce, sizeof(zero_cce));
    assert(decode_cce(&bs, dec) == FAAD_OK);
    assert(dec->coupling && dec->allocated_bytes == total);
    size_t before = calls;
    assert(faad_decoder_flush(dec) == FAAD_OK);
    bits_init(&bs, zero_cce, sizeof(zero_cce));
    assert(decode_cce(&bs, dec) == FAAD_OK && calls == before);
    assert(faad_decoder_close(&dec) == FAAD_OK && !live);
}
#endif

int main(void)
{
#if MAX_CHANNELS >= 3
    test_coupling_allocation();
#endif
#ifndef FAAD_DISABLE_SBR
    test_lazy_allocations(false);
#ifndef FAAD_DISABLE_PS
    test_lazy_allocations(true);
#endif
    calls = live = total = largest = fail_at = 0;
#endif
    faad_config cfg;
    assert(faad_config_init(&cfg, sizeof(cfg)) == FAAD_OK);
    faad_decoder *dec = NULL;
    assert(faad_decoder_open(&cfg, NULL, 0, &dec) == FAAD_OK);
    size_t plain_total = total;
    assert(faad_decoder_close(&dec) == FAAD_OK && !live);
    calls = live = total = largest = 0;
    static const uint8_t he_asc[] = { 0x14, 0x08, 0x56, 0xe5, 0xa8 };
    cfg.stream_format = FAAD_STREAM_RAW;
    assert(faad_decoder_open(&cfg, he_asc, sizeof(he_asc), &dec) == FAAD_OK);
#ifndef FAAD_DISABLE_SBR
    assert(total > plain_total);
#endif
    assert(dec && live == calls);
    assert(dec->allocated_bytes == total && dec->largest_allocation == largest);
#if MAX_CHANNELS == 2 && !defined(FAAD_DISABLE_SBR)
    /* HE-v1 must fit the embedded budget even when PS support is compiled in. */
    assert(total <= 100 * 1024);
    assert(largest <= 48 * 1024);
#endif
    size_t allocs = calls;
#ifndef FAAD_DISABLE_PS
    size_t he_total = total;
#endif
    printf("allocations=%zu total=%zu largest=%zu\n", calls, total, largest);
    assert(faad_decoder_close(&dec) == FAAD_OK && !dec && !live);
    for (size_t failure = 1; failure <= allocs; failure++) {
        calls = live = total = largest = 0;
        fail_at = failure;
        assert(faad_decoder_open(&cfg, he_asc, sizeof(he_asc), &dec) == FAAD_ERR_NO_MEMORY);
        assert(!dec && !live);
    }
#ifndef FAAD_DISABLE_PS
    static const uint8_t ps_asc[] = { 0xec, 0x0a, 0x88, 0x00 };
    calls = live = total = largest = fail_at = 0;
    assert(faad_decoder_open(&cfg, ps_asc, sizeof(ps_asc), &dec) == FAAD_OK);
    assert(total > he_total);
    assert(dec->ps);
    allocs = calls;
    assert(faad_decoder_close(&dec) == FAAD_OK && !live);
    for (size_t failure = 1; failure <= allocs; failure++) {
        calls = live = total = largest = 0;
        fail_at = failure;
        assert(faad_decoder_open(&cfg, ps_asc, sizeof(ps_asc), &dec) == FAAD_ERR_NO_MEMORY);
        assert(!dec && !live);
    }
#endif
    return 0;
}
