/*
 * FAAR - Freeware Advanced Audio Resampler
 * Copyright (C) 2026 Nils Schimmelmann
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 */
#include <faar_alloc_hooks.h>
#include <faar.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TOTAL_INPUT 2305
#define OUT_MAX 4096

typedef struct {
    float *data;
    size_t frames;
    size_t capacity;
} pcm_t;

typedef struct {
    uint32_t in_rate, out_rate;
    uint16_t channels;
    faar_quality quality;
    uint32_t decimate;
    size_t block, output_cap;
    int repeat_prepare;
} test_case;

static int fail(const char *message, size_t which) {
    fprintf(stderr, "test_faar_bounded: %s (case %zu)\n", message, which);
    return 1;
}

static size_t expected_frames(size_t input, uint32_t in_rate, uint32_t out_rate) {
    return (size_t)(((uint64_t)input * out_rate + in_rate - 1) / in_rate);
}

static int append(pcm_t *pcm, const float *src, size_t frames, size_t channels) {
    if (!frames) return 1;
    if (frames > SIZE_MAX - pcm->frames) return 0;
    size_t need = pcm->frames + frames;
    if (need > pcm->capacity) {
        size_t capacity = pcm->capacity ? pcm->capacity : 64;
        while (capacity < need) {
            if (capacity > SIZE_MAX / 2) return 0;
            capacity *= 2;
        }
        if (capacity > SIZE_MAX / channels / sizeof(float)) return 0;
        float *next = realloc(pcm->data, capacity * channels * sizeof(float));
        if (!next) return 0;
        pcm->data = next;
        pcm->capacity = capacity;
    }
    memcpy(pcm->data + pcm->frames * channels, src,
           frames * channels * sizeof(float));
    pcm->frames += frames;
    return 1;
}

static float input_sample(size_t frame, size_t channel) {
    uint32_t x = (uint32_t)(frame * UINT32_C(2654435761)
                          + channel * UINT32_C(2246822519) + UINT32_C(3266489917));
    x ^= x >> 15; x *= UINT32_C(2246822519); x ^= x >> 13;
    return (float)((int32_t)(x & UINT32_C(0x00ffffff)) - INT32_C(0x00800000))
        / 8388608.0f;
}

static int drain(faar_resampler *r, size_t cap, uint16_t channels, pcm_t *pcm) {
    float out[OUT_MAX * 8];
    for (size_t guard = 0; guard < 100000; ++guard) {
        size_t made = 0;
        faar_status status = faar_resampler_process(r, NULL, 0, out, cap, &made);
        if (status != FAAR_OK || made > cap || !append(pcm, out, made, channels))
            return 0;
        if (!made) return 1;
    }
    return 0;
}

static int stream_all(faar_resampler *r, const test_case *tc, size_t bound,
                      const float *input, pcm_t *pcm) {
    float out[OUT_MAX * 8];
    size_t offset = 0;
    while (offset < TOTAL_INPUT) {
        size_t n = tc->repeat_prepare ? bound : tc->block;
        if (n > bound) n = bound;
        if (n > TOTAL_INPUT - offset) n = TOTAL_INPUT - offset;
        size_t made = 0;
        if (faar_resampler_process(r, input + offset * tc->channels, n,
                                   out, tc->output_cap, &made) != FAAR_OK ||
            made > tc->output_cap || !append(pcm, out, made, tc->channels))
            return 0;
        offset += n;
        if (!drain(r, tc->output_cap, tc->channels, pcm)) return 0;
    }
    for (size_t guard = 0; guard < 100000; ++guard) {
        size_t made = 0;
        if (faar_resampler_finalize(r, out, tc->output_cap, &made) != FAAR_OK ||
            made > tc->output_cap || !append(pcm, out, made, tc->channels)) return 0;
        if (!made) return 1;
    }
    return 0;
}

static int open_case(const test_case *tc, size_t bound, faar_resampler **resampler) {
    faar_config config;
    if (faar_config_init(&config, sizeof(config)) != FAAR_OK) return 0;
    config.in_rate = tc->in_rate;
    config.out_rate = tc->out_rate;
    config.channels = tc->channels;
    config.quality = tc->quality;
    config.decimate_factor = tc->decimate;
    config.max_input_frames = (uint32_t)bound;
    return faar_resampler_open(&config, resampler) == FAAR_OK && *resampler;
}

static int run_comparison(const test_case *tc, size_t which, pcm_t *reference_out) {
    faar_resampler *reference = NULL, *prepared = NULL;
    pcm_t ref = {0}, got = {0};
    float *input = malloc(TOTAL_INPUT * tc->channels * sizeof(float));
    if (!input) return fail("input allocation failed", which);
    for (size_t i = 0; i < TOTAL_INPUT; ++i)
        for (size_t c = 0; c < tc->channels; ++c)
            input[i * tc->channels + c] = input_sample(i, c);
    size_t bound = tc->block + (tc->repeat_prepare ? 19 : 0);
    if (!open_case(tc, 0, &reference) || !open_case(tc, bound, &prepared)) goto error;
    faar_alloc_reset_counters();
    size_t ref_calls_before = faar_alloc_calls(), ref_frees_before = faar_alloc_frees();
    if (!stream_all(reference, tc, bound, input, &ref)) goto error;
    printf("unreserved %u->%u channels=%u block=%zu cap=%zu: allocations=%zu frees=%zu\n",
           tc->in_rate, tc->out_rate, tc->channels, tc->block, tc->output_cap,
           faar_alloc_calls() - ref_calls_before, faar_alloc_frees() - ref_frees_before);

    /* An over-bound call must fail before consuming any stream state. */
    float too_much[1025 * 8] = {0}, scratch[OUT_MAX * 8];
    size_t made = 123;
    faar_alloc_reset_counters();
    if (faar_resampler_process(prepared, too_much, bound + 1, scratch,
                               tc->output_cap, &made) != FAAR_ERR_INVALID_ARGUMENT ||
        made != 0 || faar_alloc_calls() != 0) goto error;

    size_t before_calls = faar_alloc_calls(), before_frees = faar_alloc_frees();
    faar_alloc_fail_all(1);
    int got_ok = stream_all(prepared, tc, bound, input, &got);
    size_t after_calls = faar_alloc_calls(), after_frees = faar_alloc_frees();
    faar_alloc_fail_all(0);
    if (!got_ok || after_calls != before_calls ||
        after_frees != before_frees || ref.frames != got.frames ||
        got.frames != expected_frames(TOTAL_INPUT, tc->in_rate, tc->out_rate) ||
        memcmp(ref.data, got.data, got.frames * tc->channels * sizeof(float)))
        goto error;

    if (reference_out) *reference_out = ref;
    else free(ref.data);
    free(got.data); free(input);
    faar_resampler_close(&reference); faar_resampler_close(&prepared);
    return 0;
error:
    faar_alloc_fail_all(0);
    free(ref.data); free(got.data); free(input);
    if (reference) faar_resampler_close(&reference);
    if (prepared) faar_resampler_close(&prepared);
    return fail("reserved stream mismatch, invalid state, or allocation occurred", which);
}

static int test_reserve_failures(const test_case *tc, const pcm_t *expected) {
    size_t live_at_start = faar_alloc_live();
    faar_resampler *probe = NULL;
    faar_alloc_reset_counters();
    if (!open_case(tc, tc->block, &probe)) return fail("allocation probe open failed", 99);
    size_t allocation_points = faar_alloc_calls();
    faar_resampler_close(&probe);
    if (faar_alloc_live() != live_at_start) return fail("allocation probe leaked", 99);

    float *input = malloc(TOTAL_INPUT * tc->channels * sizeof(float));
    if (!input) return fail("failure test input allocation failed", 99);
    for (size_t i = 0; i < TOTAL_INPUT; ++i)
        for (size_t c = 0; c < tc->channels; ++c)
            input[i * tc->channels + c] = input_sample(i, c);

    for (size_t nth = 1; nth <= allocation_points; ++nth) {
        faar_resampler *r = NULL;
        pcm_t actual = {0};
        size_t live_before = faar_alloc_live();
        faar_config config;
        faar_config_init(&config, sizeof(config));
        config.in_rate = tc->in_rate;
        config.out_rate = tc->out_rate;
        config.channels = tc->channels;
        config.quality = tc->quality;
        config.decimate_factor = tc->decimate;
        config.max_input_frames = (uint32_t)tc->block;
        faar_alloc_reset_counters();
        faar_alloc_fail_nth(nth);
        faar_status status = faar_resampler_open(&config, &r);
        faar_alloc_fail_nth(0);
        if (status != FAAR_ERR_NO_MEMORY || r || faar_alloc_live() != live_before ||
            !open_case(tc, tc->block, &r) ||
            !stream_all(r, tc, tc->block, input, &actual) ||
            actual.frames != expected->frames ||
            memcmp(actual.data, expected->data,
                   expected->frames * tc->channels * sizeof(float))) {
            free(actual.data); faar_resampler_close(&r); free(input);
            return fail("reserving open failure leaked or the retry changed PCM", nth);
        }
        free(actual.data);
        faar_resampler_close(&r);
        if (faar_alloc_live() != live_before) {
            free(input);
            return fail("reserving open leaked library allocations", nth);
        }
    }
    free(input);
    return 0;
}

static int test_budget_exhaustion(void) {
    test_case tc = {48000,48000,1,FAAR_QUALITY_GOOD,0,64,0,0};
    faar_resampler *r = NULL;
    float input[64], output[1];
    size_t made = 0;
    for (size_t i = 0; i < 64; ++i) input[i] = input_sample(i, 0);
    if (!open_case(&tc, 64, &r)) {
        return fail("budget exhaustion setup failed", 97);
    }
    faar_alloc_reset_counters();
    faar_alloc_fail_all(1);
    faar_status status = FAAR_OK;
    for (size_t block = 0; block < 256 && status == FAAR_OK; ++block)
        status = faar_resampler_process(r, input, 64, output, 0, &made);
    size_t calls = faar_alloc_calls();
    faar_alloc_fail_all(0);
    faar_resampler_close(&r);
    if ((status != FAAR_ERR_NO_MEMORY && status != FAAR_ERR_PROCESSING) || calls)
        return fail("undrained bounded output did not exhaust without allocation", 97);
    return 0;
}

static int test_high_open_failures(void) {
    const uint32_t rates[][2] = {{44100,48000}, {192000,44100}};
    faar_library_info info = {.struct_size = sizeof(info)};
    if (faar_get_library_info(&info) != FAAR_OK) return fail("library query failed", 98);
    for (size_t pair = 0; pair < sizeof(rates) / sizeof(rates[0]); ++pair) {
        faar_config config;
        if (faar_config_init(&config, sizeof(config)) != FAAR_OK) return 1;
        config.in_rate = rates[pair][0]; config.out_rate = rates[pair][1];
        config.quality = FAAR_QUALITY_HIGH;
        config.channels = (uint16_t)(info.max_channels < 3 ? info.max_channels : 3);
        faar_resampler *r = NULL;
        size_t live_before = faar_alloc_live();
        faar_alloc_reset_counters();
        if (faar_resampler_open(&config, &r) != FAAR_OK) return fail("High open failed", 98);
        size_t calls = faar_alloc_calls();
        faar_resampler_close(&r);
        for (size_t nth = 1; nth <= calls; ++nth) {
            faar_alloc_reset_counters();
            faar_alloc_fail_nth(nth);
            faar_status status = faar_resampler_open(&config, &r);
            faar_alloc_fail_nth(0);
            int returned_handle = r != NULL;
            faar_resampler_close(&r);
            if (status != FAAR_ERR_NO_MEMORY || returned_handle
                    || faar_alloc_live() != live_before)
                return fail("High allocation failure leaked or returned a handle", nth);
        }
    }
    return 0;
}

int main(void) {
    static const test_case cases[] = {
        {48000,44100,1,FAAR_QUALITY_GOOD,0,64,1,1},
        {44100,48000,3,FAAR_QUALITY_HIGH,0,1024,17,0},
        {192000,44100,8,FAAR_QUALITY_GOOD,0,64,4096,0},
        {384000,44100,3,FAAR_QUALITY_HIGH,0,1024,1,0},
        {48000,48000,8,FAAR_QUALITY_GOOD,0,64,17,0},
        {47999,44100,1,FAAR_QUALITY_HIGH,0,1024,4096,0},
        {96000,48000,3,FAAR_QUALITY_GOOD,2,64,17,0},
        {8000,192000,3,FAAR_QUALITY_GOOD,0,17,1,0},
        {44100,8000,3,FAAR_QUALITY_HIGH,0,257,17,0},
        {47999,32001,3,FAAR_QUALITY_GOOD,0,17,17,0},
        {11025,48000,1,FAAR_QUALITY_GOOD,0,1,4096,0},
        {48000,44100,8,FAAR_QUALITY_HIGH,0,64,1,0},
        {192000,8000,3,FAAR_QUALITY_GOOD,0,1024,1,0}
    };
    faar_library_info library = {.struct_size = sizeof(library)};
    if (faar_get_library_info(&library) != FAAR_OK) return fail("library info query failed", 0);
    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
        test_case selected = cases[i];
        if (selected.channels > library.max_channels)
            selected.channels = (uint16_t)library.max_channels;
        pcm_t reference = {0};
        if (run_comparison(&selected, i, &reference)) return 1;
        if ((i == 0 || i == 1 || i == 2) && test_reserve_failures(&selected, &reference)) {
            free(reference.data); return 1;
        }
        free(reference.data);
    }
    if (test_budget_exhaustion() || test_high_open_failures()) return 1;
    return 0;
}
