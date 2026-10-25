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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */
#include <faar.h>
#include "faar_test_api.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_groups.h"

#ifndef MAX_CHANNELS
#define MAX_CHANNELS 8
#endif

#define FRAME_COUNT 257
#define STREAM_BLOCK 17
#define STREAM_OUTPUT 19

static const uint32_t rates[] = {
    8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000,
    88200, 96000, 176400, 192000, 352800, 384000, 705600, 768000
};

typedef struct {
    float *data;
    size_t frames;
} result_t;

static int fail(const char *why, uint32_t input_rate, uint32_t output_rate) {
    fprintf(stderr, "%s (%u -> %u Hz)\n", why, input_rate, output_rate);
    return 1;
}

static size_t frame_total(size_t input_frames, uint32_t input_rate,
                          uint32_t output_rate) {
    uint64_t scaled = (uint64_t)input_frames * output_rate;
    return (size_t)((scaled + input_rate - 1) / input_rate);
}

static int append(result_t *result, size_t *capacity, const float *samples,
                  size_t frames) {
    if (frames > SIZE_MAX - result->frames) return 0;
    size_t needed = result->frames + frames;
    if (needed > *capacity) {
        size_t grown = *capacity ? *capacity : 32;
        while (grown < needed) {
            if (grown > SIZE_MAX / 2) return 0;
            grown *= 2;
        }
        if (grown > SIZE_MAX / sizeof(float)) return 0;
        float *next = realloc(result->data, grown * sizeof(float));
        if (!next) return 0;
        result->data = next;
        *capacity = grown;
    }
    if (frames)
        memcpy(result->data + result->frames, samples, frames * sizeof(float));
    result->frames += frames;
    return 1;
}

static float input_sample(size_t index) {
    uint32_t value = (uint32_t)index * UINT32_C(2654435761)
        + UINT32_C(2246822519);
    return (float)(value >> 8) / 8388608.0f - 1.0f;
}

static int convert(const float *input, uint32_t input_rate,
                   uint32_t output_rate, faar_quality quality,
                   int chunked, result_t *result) {
    faar_test_handle * handle = faar_test_create(input_rate, output_rate, 1, quality);
    if (!handle) return fail("resampler creation failed", input_rate, output_rate);

    size_t output_capacity = chunked ? STREAM_OUTPUT
                                    : frame_total(FRAME_COUNT, input_rate,
                                                  output_rate) + 16;
    float *buffer = malloc(output_capacity * sizeof(float));
    if (!buffer) {
        faar_test_close(handle);
        return fail("output allocation failed", input_rate, output_rate);
    }

    size_t capacity = 0;
    for (size_t offset = 0; offset < FRAME_COUNT;) {
        size_t count = chunked ? STREAM_BLOCK : FRAME_COUNT;
        if (count > FRAME_COUNT - offset) count = FRAME_COUNT - offset;
        size_t made = faar_test_process(handle, input + offset, count, buffer,
                                   output_capacity);
        if (!append(result, &capacity, buffer, made)) goto allocation_error;
        offset += count;

        if (chunked) {
            for (;;) {
                made = faar_test_process(handle, NULL, 0, buffer, output_capacity);
                if (!made) break;
                if (!append(result, &capacity, buffer, made))
                    goto allocation_error;
            }
        }
    }

    for (;;) {
        size_t made = faar_test_flush(handle, buffer, output_capacity);
        if (!made) break;
        if (!append(result, &capacity, buffer, made)) goto allocation_error;
    }

    free(buffer);
    faar_test_close(handle);
    return 0;

allocation_error:
    free(buffer);
    free(result->data);
    result->data = NULL;
    result->frames = 0;
    faar_test_close(handle);
    return fail("result allocation failed", input_rate, output_rate);
}

static size_t rate_pairs(uint32_t pairs[][2], size_t capacity) {
    size_t count = 0;
    for (size_t in = 0; in < sizeof(rates) / sizeof(rates[0]); ++in) {
        for (size_t out = 0; out < sizeof(rates) / sizeof(rates[0]); ++out) {
            if (count == capacity) return 0;
            pairs[count][0] = rates[in];
            pairs[count++][1] = rates[out];
        }
    }
    for (size_t i = 0; i < sizeof(rates) / sizeof(rates[0]); ++i) {
        for (int direction = -1; direction <= 1; direction += 2) {
            uint32_t near_rate = rates[i] + direction;
            if (count + 2 > capacity) return 0;
            pairs[count][0] = rates[i]; pairs[count++][1] = near_rate;
            pairs[count][0] = near_rate; pairs[count++][1] = rates[i];
        }
    }
    return count;
}

static int test_rate_matrix_partition(size_t partition) {
    uint32_t pairs[357][2];
    size_t pair_count = rate_pairs(pairs, 357);
    size_t covered = 0;
    float input[FRAME_COUNT];
    for (size_t i = 0; i < FRAME_COUNT; ++i) input[i] = input_sample(i);

    if (partition >= 3 || pair_count != 357) return 1;
    for (size_t p = 0; p < pair_count; ++p) {
        size_t group = p < 289 ? p * 3 / 289 : (p - 289) * 3 / 68;
        if (group != partition) continue;
        ++covered;
        uint32_t in_rate = pairs[p][0], out_rate = pairs[p][1];
        for (int preset = FAAR_QUALITY_FAST; preset <= FAAR_QUALITY_HIGH;
             ++preset) {
            /* Exercise every standard pair at GOOD quality. The one-hertz
             * boundary pairs run at each available preset. */
            if (p < 289 && preset != FAAR_QUALITY_GOOD) continue;
            result_t whole = {0}, chunked = {0};
            if (convert(input, in_rate, out_rate, (faar_quality)preset, 0,
                        &whole) ||
                convert(input, in_rate, out_rate, (faar_quality)preset, 1,
                        &chunked)) {
                free(whole.data); free(chunked.data);
                return 1;
            }
            size_t expected = frame_total(FRAME_COUNT, in_rate, out_rate);
            if (whole.frames != expected || chunked.frames != expected) {
                free(whole.data); free(chunked.data);
                return fail("rate matrix produced an unexpected frame count",
                            in_rate, out_rate);
            }
            for (size_t i = 0; i < expected; ++i) {
                if (!isfinite(whole.data[i]) || !isfinite(chunked.data[i]) ||
                    fabsf(whole.data[i] - chunked.data[i]) > 2e-6f) {
                    free(whole.data); free(chunked.data);
                    return fail("stream block size changed output", in_rate,
                                out_rate);
                }
            }
            free(whole.data); free(chunked.data);
        }
    }
    printf("FAAR rate matrix partition %zu passed\n", partition);
    return covered == (partition == 0 ? 120 : partition == 1 ? 119 : 118)
        ? 0 : fail("rate-matrix partition is incomplete", 0, 0);
}

static int test_rate_matrix_0(void) { return test_rate_matrix_partition(0); }
static int test_rate_matrix_1(void) { return test_rate_matrix_partition(1); }
static int test_rate_matrix_2(void) { return test_rate_matrix_partition(2); }

static int test_equal_rate_identity(void) {
    float input[64], output[64];
    for (size_t i = 0; i < 64; ++i) input[i] = input_sample(i);
    input[0] = 0.0f;
    input[1] = -0.0f;

    faar_test_handle * handle = faar_test_create(48000, 48000, 1, FAAR_QUALITY_GOOD);
    if (!handle) return fail("equal-rate setup failed", 48000, 48000);
    size_t made = faar_test_process(handle, input, 64, output, 64);
    size_t tail = faar_test_flush(handle, output + made, 64 - made);
    int matches = made + tail == 64 && memcmp(input, output, sizeof(input)) == 0;
    faar_test_close(handle);
    return matches ? 0 : fail("equal-rate conversion changed samples", 48000,
                              48000);
}

/* Interleaved channels must match independent mono runs, including the
 * six-channel input path and the -D cascade. */
static int check_independence(uint32_t in_rate, uint32_t out_rate,
        uint16_t channels, uint32_t factor) {
    enum { MAX_TEST_CHANNELS = 8 };
    if (channels > MAX_CHANNELS) {
        puts("channel count exceeds the configured maximum; skipped");
        return 0;
    }
    static float interleaved[FRAME_COUNT * MAX_TEST_CHANNELS];
    static float joined[(FRAME_COUNT + 16) * MAX_TEST_CHANNELS];
    static float mono_in[FRAME_COUNT], mono_out[FRAME_COUNT + 16];
    const size_t capacity = FRAME_COUNT + 16;
    for (size_t i = 0; i < FRAME_COUNT; ++i)
        for (uint16_t c = 0; c < channels; ++c)
            interleaved[i * channels + c] = input_sample(i + 901u * c);

    faar_test_handle * combined = faar_test_create_ext(in_rate, out_rate,
        channels, FAAR_QUALITY_GOOD, factor);
    if (!combined) return fail("multichannel setup failed", in_rate, out_rate);
    size_t joined_frames = faar_test_process(combined, interleaved, FRAME_COUNT,
                                             joined, capacity);
    joined_frames += faar_test_flush(combined,
        joined + joined_frames * channels, capacity - joined_frames);
    faar_test_close(combined);
    if (joined_frames != frame_total(FRAME_COUNT, in_rate, out_rate))
        return fail("channel test produced an unexpected frame count",
                    in_rate, out_rate);

    for (uint16_t c = 0; c < channels; ++c) {
        for (size_t i = 0; i < FRAME_COUNT; ++i) mono_in[i] = interleaved[i * channels + c];
        faar_test_handle * mono = faar_test_create_ext(in_rate, out_rate, 1,
            FAAR_QUALITY_GOOD, factor);
        if (!mono) return fail("mono setup failed", in_rate, out_rate);
        size_t made = faar_test_process(mono, mono_in, FRAME_COUNT, mono_out, capacity);
        made += faar_test_flush(mono, mono_out + made, capacity - made);
        faar_test_close(mono);
        if (made != joined_frames || made == 0)
            return fail("channel test produced an unexpected frame count",
                        in_rate, out_rate);
        for (size_t i = 0; i < made; ++i)
            if (fabsf(joined[i * channels + c] - mono_out[i]) > 2e-6f)
                return fail("other channels changed resampled output",
                            in_rate, out_rate);
    }
    return 0;
}

static int test_channel_independence(void) {
    static const uint16_t counts[] = {2, 3, 6};
    for (size_t i = 0; i < sizeof(counts) / sizeof(counts[0]); ++i)
        if (check_independence(96000, 44100, counts[i], 0)) return 1;
    return 0;
}

static int test_decimate_cascade(void) {
    for (uint32_t factor = 2; factor <= 4; factor += 2)
        if (check_independence(96000, 22050, MAX_CHANNELS >= 2 ? 2 : 1, factor))
            return 1;
    /* Factors too large for the target fall back to a nearer intermediate rate. */
    return check_independence(96000, 48000, MAX_CHANNELS >= 2 ? 2 : 1, 4)
        || check_independence(48000, 44100, MAX_CHANNELS >= 2 ? 2 : 1, 4);
}

int main(int argc, char **argv) {
    const faar_test_group groups[] = {
        {"matrix-0", test_rate_matrix_0},
        {"matrix-1", test_rate_matrix_1},
        {"matrix-2", test_rate_matrix_2},
        {"equal-rate", test_equal_rate_identity},
        {"channels", test_channel_independence},
        {"decimate", test_decimate_cascade}
    };
    return faar_run_groups(argc, argv, groups,
                           sizeof(groups) / sizeof(groups[0]));
}
