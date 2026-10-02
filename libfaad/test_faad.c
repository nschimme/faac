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

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#if defined(_WIN32) && !defined(__MINGW32__)
#include <windows.h>
#else
#include <pthread.h>
#endif

#include "faad.h"
#include "config.h"

#define NUM_THREADS 8
#define ITERATIONS_PER_THREAD 100

static void run_decoder_iteration(void)
{
    faad_config cfg;
    faad_status st = faad_config_init(&cfg, sizeof(cfg));
    assert(st == FAAD_OK);

    faad_decoder *dec = NULL;
    st = faad_decoder_open(&cfg, NULL, 0, &dec);
    assert(st == FAAD_OK);
    assert(dec != NULL);

    faad_stream_info info = { .struct_size = sizeof(faad_stream_info) };
    st = faad_decoder_get_info(dec, &info);
    assert(st == FAAD_OK);

    st = faad_decoder_flush(dec);
    assert(st == FAAD_OK);

    faad_decoder_close(&dec);
}

#if defined(_WIN32) && !defined(__MINGW32__)
static DWORD WINAPI thread_test_worker(LPVOID arg)
{
    (void)arg;
    for (int i = 0; i < ITERATIONS_PER_THREAD; i++) {
        run_decoder_iteration();
    }
    return 0;
}
#else
static void *thread_test_worker(void *arg)
{
    (void)arg;
    for (int i = 0; i < ITERATIONS_PER_THREAD; i++) {
        run_decoder_iteration();
    }
    return NULL;
}
#endif

/* AudioSpecificConfig signalling: the 0x2b7 sync extension carries a real
 * sbrPresentFlag, so an explicit "no SBR" (FFmpeg's default for AAC-LC in
 * MP4) must stay LC at the core rate, while faac's HE-AAC form and the
 * explicit hierarchical AOT 5/29 forms enable it. */
static void test_asc_sbr_signalling(void)
{
    static const uint8_t lc_explicit_no_sbr[] = { 0x14, 0x08, 0x56, 0xe5, 0x00 };
    static const uint8_t lc_plain[]           = { 0x14, 0x08 };
    static const uint8_t he_sbr_present[]     = { 0x14, 0x08, 0x56, 0xe5, 0xa8 };
    /* explicit hierarchical: AOT 5 / 29, 16 kHz, mono, 32 kHz out, core LC */
    static const uint8_t he_hierarchical[]    = { 0x2c, 0x0a, 0x88, 0x00 };
    static const uint8_t hev2_hierarchical[]  = { 0xec, 0x0a, 0x88, 0x00 };
    const struct { const uint8_t *asc; uint32_t len; enum faad_object_type obj; uint32_t rate; } cases[] = {
        { lc_explicit_no_sbr, sizeof(lc_explicit_no_sbr), FAAD_OBJ_LC,        16000 },
        { lc_plain,           sizeof(lc_plain),           FAAD_OBJ_LC,        16000 },
        { he_sbr_present,     sizeof(he_sbr_present),     FAAD_OBJ_HE_AAC_V1, 32000 },
        { he_hierarchical,    sizeof(he_hierarchical),    FAAD_OBJ_HE_AAC_V1, 32000 },
        { hev2_hierarchical,  sizeof(hev2_hierarchical),  FAAD_OBJ_HE_AAC_V2, 32000 },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        faad_config cfg;
        assert(faad_config_init(&cfg, sizeof(cfg)) == FAAD_OK);
        cfg.stream_format = FAAD_STREAM_RAW;
        faad_decoder *dec = NULL;
        assert(faad_decoder_open(&cfg, cases[i].asc, cases[i].len, &dec) == FAAD_OK);
        faad_stream_info info = { .struct_size = sizeof(faad_stream_info) };
        assert(faad_decoder_get_info(dec, &info) == FAAD_OK);
#ifdef FAAD_DISABLE_SBR
        assert(info.sample_rate == 16000);
        assert(info.frame_samples == 1024);
#else
        assert(info.sample_rate == cases[i].rate);
        assert(info.frame_samples == (cases[i].obj == FAAD_OBJ_LC ? 1024u : 2048u));
#endif
#if defined(FAAD_DISABLE_PS) || defined(FAAD_DISABLE_SBR) || MAX_CHANNELS < 2
        assert(info.channels == 1);
#else
        assert(info.channels == (cases[i].obj == FAAD_OBJ_HE_AAC_V2 ? 2u : 1u));
#endif
        assert(info.object_type == cases[i].obj);
        faad_decoder_close(&dec);
    }
}

static void test_asc_drm_rejection(void)
{
    /* AAC-LC with frameLengthFlag = 1 (960-sample DRM frame size) */
    static const uint8_t drm_frame_len_960[] = { 0x14, 0x0c };
    /* ER AAC LC (AOT 17) */
    static const uint8_t drm_er_aot17[]       = { 0x8c, 0x08 };

    const struct { const uint8_t *asc; uint32_t len; } drm_cases[] = {
        { drm_frame_len_960, sizeof(drm_frame_len_960) },
        { drm_er_aot17,       sizeof(drm_er_aot17) },
    };

    faad_config cfg;
    assert(faad_config_init(&cfg, sizeof(cfg)) == FAAD_OK);
    cfg.stream_format = FAAD_STREAM_RAW;

    for (size_t i = 0; i < sizeof(drm_cases) / sizeof(drm_cases[0]); i++) {
        faad_decoder *dec = NULL;
        faad_status st = faad_decoder_open(&cfg, drm_cases[i].asc, drm_cases[i].len, &dec);
        assert(st == FAAD_ERR_UNSUPPORTED);
        assert(dec == NULL);
    }
}

static void test_asc_unlisted_core_rate_rejection(void)
{
    /* AAC-LC, sampling_frequency_index 15 (escape) with an explicit 40000 Hz:
     * no sfb layout exists for it, so it must be refused, not decoded with
     * the 44.1 kHz tables */
    static const uint8_t lc_40000[] = { 0x17, 0x80, 0x4e, 0x20, 0x08 };

    faad_config cfg;
    assert(faad_config_init(&cfg, sizeof(cfg)) == FAAD_OK);
    cfg.stream_format = FAAD_STREAM_RAW;

    faad_decoder *dec = NULL;
    assert(faad_decoder_open(&cfg, lc_40000, sizeof(lc_40000), &dec) == FAAD_ERR_UNSUPPORTED);
    assert(dec == NULL);
}

static void test_struct_sizes_and_enums(void)
{
    struct { faad_config cfg; uint32_t guard; } c;
    memset(&c, 0xa5, sizeof(c));
    assert(faad_config_init(&c.cfg, sizeof(c.cfg) - 1) == FAAD_ERR_INVALID_ARGUMENT);
    assert(c.cfg.struct_size == 0xa5a5a5a5u);
    assert(faad_config_init(&c.cfg, sizeof(c)) == FAAD_OK);
    assert(c.cfg.struct_size == sizeof(c.cfg));
    assert(c.guard == 0xa5a5a5a5u);
    faad_decoder *dec = NULL;
    c.cfg.struct_size = sizeof(c);
    assert(faad_decoder_open(&c.cfg, NULL, 0, &dec) == FAAD_OK);
    struct { faad_stream_info info; uint32_t guard; } out;
    memset(&out, 0xa5, sizeof(out));
    out.info.struct_size = sizeof(out.info) - 1;
    assert(faad_decoder_get_info(dec, &out.info) == FAAD_ERR_INVALID_ARGUMENT);
    assert(out.info.sample_rate == 0xa5a5a5a5u);
    out.info.struct_size = sizeof(out);
    assert(faad_decoder_get_info(dec, &out.info) == FAAD_OK);
    assert(out.info.struct_size == sizeof(out.info));
    assert(out.guard == 0xa5a5a5a5u);
    assert(faad_decoder_close(&dec) == FAAD_OK && dec == NULL);
    uint32_t bytes;
    assert(faad_get_state_size(NULL, &bytes) == FAAD_OK);
    void *mem = malloc(bytes);
    assert(mem != NULL);
    for (int i = 0; i < 4; i++) {
        faad_config_init(&c.cfg, sizeof(c.cfg));
        if (i == 0) c.cfg.struct_size--;
        if (i == 1) c.cfg.stream_format = FAAD_STREAM_MAX;
        if (i == 2) c.cfg.output_format = FAAD_OUTPUT_MAX;
        if (i == 3) c.cfg.downmix_mode = FAAD_DOWNMIX_MAX;
        assert(faad_decoder_open(&c.cfg, NULL, 0, &dec) == FAAD_ERR_INVALID_ARGUMENT);
        assert(dec == NULL);
        assert(faad_decoder_init(mem, bytes, &c.cfg, NULL, 0, &dec) == FAAD_ERR_INVALID_ARGUMENT);
        assert(dec == NULL);
    }
    free(mem);
}

int main(void)
{
    test_struct_sizes_and_enums();
    test_asc_sbr_signalling();
    test_asc_drm_rejection();
    test_asc_unlisted_core_rate_rejection();

    faad_config cfg;
    faad_status st = faad_config_init(&cfg, sizeof(cfg));
    assert(st == FAAD_OK);
    assert(cfg.stream_format == FAAD_STREAM_ADTS);

    uint32_t state_bytes = 0;
    st = faad_get_state_size(&cfg, &state_bytes);
    assert(st == FAAD_OK);
    assert(state_bytes > 0);

    void *static_mem = malloc(state_bytes);
    assert(static_mem != NULL);

    faad_decoder *dec_static = NULL;
    st = faad_decoder_init(static_mem, state_bytes, &cfg, NULL, 0, &dec_static);
    assert(st == FAAD_OK);
    assert(dec_static != NULL);

    faad_stream_info info = { .struct_size = sizeof(faad_stream_info) };
    st = faad_decoder_get_info(dec_static, &info);
    assert(st == FAAD_OK);
    assert(info.channels == 2);

    st = faad_decoder_flush(dec_static);
    assert(st == FAAD_OK);

    assert(faad_decoder_close(&dec_static) == FAAD_OK);
    assert(dec_static == NULL);
    free(static_mem);

    faad_decoder *dec_heap = NULL;
    st = faad_decoder_open(&cfg, NULL, 0, &dec_heap);
    assert(st == FAAD_OK);
    assert(dec_heap != NULL);

    st = faad_decoder_flush(dec_heap);
    assert(st == FAAD_OK);

    assert(faad_decoder_close(&dec_heap) == FAAD_OK);
    assert(dec_heap == NULL);
    assert(faad_decoder_close(&dec_heap) == FAAD_OK);
    assert(faad_decoder_close(NULL) == FAAD_ERR_INVALID_ARGUMENT);

#if defined(_WIN32) && !defined(__MINGW32__)
    HANDLE threads[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        threads[i] = CreateThread(NULL, 0, thread_test_worker, NULL, 0, NULL);
        assert(threads[i] != NULL);
    }
    WaitForMultipleObjects(NUM_THREADS, threads, TRUE, INFINITE);
    for (int i = 0; i < NUM_THREADS; i++) {
        CloseHandle(threads[i]);
    }
#else
    pthread_t threads[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        int rc = pthread_create(&threads[i], NULL, thread_test_worker, NULL);
        assert(rc == 0);
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
#endif

    printf("FAAD3 static placement, heap, and concurrent multi-threading tests passed successfully.\n");
    return 0;
}
