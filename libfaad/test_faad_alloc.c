#include "faad_internal.h"
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

int main(void)
{
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
#if MAX_CHANNELS == 2 && !defined(FAAD_DISABLE_SBR) && defined(FAAD_DISABLE_PS)
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
        assert(faad_decoder_open(&cfg, he_asc, sizeof(he_asc), &dec) == FAAD_ERR_INSUFFICIENT_MEM);
        assert(!dec && !live);
    }
#ifndef FAAD_DISABLE_PS
    static const uint8_t ps_asc[] = { 0xec, 0x0a, 0x88, 0x00 };
    calls = live = total = largest = fail_at = 0;
    assert(faad_decoder_open(&cfg, ps_asc, sizeof(ps_asc), &dec) == FAAD_OK);
    assert(total > he_total);
    allocs = calls;
    assert(faad_decoder_close(&dec) == FAAD_OK && !live);
    for (size_t failure = 1; failure <= allocs; failure++) {
        calls = live = total = largest = 0;
        fail_at = failure;
        assert(faad_decoder_open(&cfg, ps_asc, sizeof(ps_asc), &dec) == FAAD_ERR_INSUFFICIENT_MEM);
        assert(!dec && !live);
    }
#endif
    return 0;
}
