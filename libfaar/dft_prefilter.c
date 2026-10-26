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
/* Centered, multichannel wrapper around the independent overlap-save FIR. */
#include "dft_prefilter.h"

#include "dft_fir.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct faar_dft_prefilter {
    uint16_t channels;
    size_t expansion;
    size_t decimation;
    size_t half;
    faar_dft_fir *fir;
    uint16_t stages;
    uint16_t lanes; /* channels sharing one stage's transforms */
    faar_sample_t *channel_input;
    faar_sample_t *channel_output;
    size_t channel_output_capacity;
    size_t block_size;
    size_t prefix_drop;
    float first[8];
    float last[8];
    float *fifo;
    size_t fifo_read;
    size_t fifo_count;
    size_t fifo_capacity;
    float *endpoint;
    size_t endpoint_capacity;
    int prepared;
    size_t input_count;
    size_t released_count;
    int have_input;
    int prefix_added;
    int finishing;
    int finished;
};

static int fifo_reserve(faar_dft_prefilter *s, size_t additional) {
    if (additional > SIZE_MAX / s->channels - s->fifo_count) return -1;
    size_t needed = s->fifo_count + additional;
    if (needed <= s->fifo_capacity) {
        if (needed > s->fifo_capacity - s->fifo_read) {
            for (uint16_t c = 0; c < s->channels; ++c)
                memmove(s->fifo + (size_t)c * s->fifo_capacity,
                        s->fifo + (size_t)c * s->fifo_capacity + s->fifo_read,
                        s->fifo_count * sizeof(float));
            s->fifo_read = 0;
        }
        return 0;
    }
    if (s->prepared) return -1;
    size_t capacity = s->fifo_capacity ? s->fifo_capacity : 4096;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2) { capacity = needed; break; }
        capacity *= 2;
    }
    if (capacity > SIZE_MAX / s->channels / sizeof(float)) return -1;
    float *next = (float *)AllocMemory(capacity * s->channels * sizeof(float));
    if (!next) return -1;
    for (uint16_t c = 0; c < s->channels; ++c)
        if (s->fifo_count)
            memcpy(next + (size_t)c * capacity,
                   s->fifo + (size_t)c * s->fifo_capacity + s->fifo_read,
                   s->fifo_count * sizeof(float));
    FreeMemory(s->fifo);
    s->fifo = next;
    s->fifo_capacity = capacity;
    s->fifo_read = 0;
    return 0;
}

size_t faar_dft_prefilter_pending(const faar_dft_prefilter *s) {
    return s->fifo_count;
}

size_t faar_dft_prefilter_drain_planes(faar_dft_prefilter *s,
        float *const output[], size_t capacity) {
    size_t n = s->fifo_count < capacity ? s->fifo_count : capacity;
    if (n)
        for (uint16_t c = 0; c < s->channels; ++c)
            memcpy(output[c],
                   s->fifo + (size_t)c * s->fifo_capacity + s->fifo_read,
                   n * sizeof(float));
    s->fifo_read += n;
    s->fifo_count -= n;
    if (!s->fifo_count) s->fifo_read = 0;
    return n;
}

static size_t fifo_drain(faar_dft_prefilter *s, float *output,
                         size_t capacity) {
    size_t n = s->fifo_count < capacity ? s->fifo_count : capacity;
    float *planes[8];
    for (uint16_t c = 0; c < s->channels; ++c)
        planes[c] = output ? output + (size_t)c * n : NULL;
    return faar_dft_prefilter_drain_planes(s, planes, capacity);
}

static int fifo_push_planes(faar_dft_prefilter *s, const faar_sample_t *planes,
                            size_t stride, size_t frames) {
    size_t drop = frames < s->prefix_drop ? frames : s->prefix_drop;
    s->prefix_drop -= drop;
    frames -= drop;
    if (!frames) return 0;
    if (fifo_reserve(s, frames)) return -1;
    size_t at = s->fifo_read + s->fifo_count;
    for (uint16_t c = 0; c < s->channels; ++c)
        for (size_t i = 0; i < frames; ++i)
            s->fifo[(size_t)c * s->fifo_capacity + at + i] =
                (float)planes[(size_t)c * stride + i + drop];
    s->fifo_count += frames;
    s->released_count += frames;
    return 0;
}

/* Feed an interleaved raw block to all independent channel FIR states. */
static int feed_raw_chunk(faar_dft_prefilter *s, const float *input,
                          size_t frames) {
    /* A feed can complete at most one stage block. */
    size_t capacity = s->channel_output_capacity;
    size_t produced = SIZE_MAX;
    for (uint16_t f = 0; f < s->stages; ++f) {
        for (uint16_t l = 0; l < s->fir[f].lanes; ++l)
            for (size_t i = 0; i < frames; ++i)
                s->channel_input[l * frames + i] =
                    input[i * s->channels + f * s->lanes + l];
        size_t consumed = 0, channel_produced = 0;
        if (faar_dft_fir_stream(&s->fir[f], s->channel_input,
                                frames, &consumed,
                                s->channel_output + f * s->lanes * capacity,
                                capacity, &channel_produced)
                || consumed != frames) return -1;
        if (produced == SIZE_MAX) produced = channel_produced;
        else if (produced != channel_produced) return -1;
    }
    return fifo_push_planes(s, s->channel_output, capacity, produced);
}

static int feed_raw(faar_dft_prefilter *s, const float *input, size_t frames) {
    while (frames) {
        size_t input_block = s->block_size / s->expansion;
        size_t count = frames < input_block ? frames : input_block;
        if (feed_raw_chunk(s, input, count)) return -1;
        input += count * s->channels;
        frames -= count;
    }
    return 0;
}

static int feed_endpoint(faar_dft_prefilter *s, const float *values,
                         size_t frames) {
    int temporary = frames > s->endpoint_capacity;
    if (temporary && s->prepared) return -1;
    float *block = temporary
        ? (float *)AllocMemory(frames * s->channels * sizeof(float)) : s->endpoint;
    if (!block) return -1;
    for (size_t i = 0; i < frames; ++i)
        memcpy(block + i * s->channels, values,
               s->channels * sizeof(float));
    int result = feed_raw(s, block, frames);
    if (temporary) FreeMemory(block);
    return result;
}

faar_dft_prefilter *faar_dft_prefilter_create(uint16_t channels,
                                               const double *taps,
                                               size_t tap_count,
                                               size_t fft_size) {
    return faar_dft_prefilter_create_expanded(channels, taps, tap_count,
                                               fft_size, 1);
}

faar_dft_prefilter *faar_dft_prefilter_create_expanded(uint16_t channels,
                                               const double *taps,
                                               size_t tap_count,
                                               size_t fft_size,
                                               size_t expansion) {
    return faar_dft_prefilter_create_rate(channels, taps, tap_count,
                                           fft_size, expansion, 1);
}

faar_dft_prefilter *faar_dft_prefilter_create_stage(uint16_t channels,
                                               const double *taps,
                                               size_t tap_count,
                                               size_t fft_size,
                                               size_t expansion,
                                               size_t decimation,
                                               int halfband, int compensated) {
    if (halfband) {
        if (tap_count > SIZE_MAX - FAAR_HALFBAND_BLOCK) return NULL;
        fft_size = FAAR_HALFBAND_BLOCK + tap_count - 1;
        expansion = 1;
        decimation = 2;
    }
    if (!channels || channels > 8 || !taps || tap_count < 3
            || !(tap_count & 1) || fft_size < tap_count
            || (expansion != 1 && expansion != 2)
            || (decimation != 1 && decimation != 2 && decimation != 4)
            || (fft_size - tap_count + 1) % expansion
            || (fft_size - tap_count + 1) % decimation
            || (tap_count - 1) % decimation)
        return NULL;
    faar_dft_prefilter *s = (faar_dft_prefilter *)faar_alloc_zeroed(1, sizeof(*s));
    if (!s) return NULL;
    s->channels = channels;
    s->expansion = expansion;
    s->decimation = decimation;
    s->half = (tap_count - 1) / 2;
    s->block_size = fft_size - tap_count + 1;
    if (s->half > SIZE_MAX / 2) { FreeMemory(s); return NULL; }
    s->prefix_drop = 2 * s->half / decimation;
    /* Two channels share each complex transform, a lone channel shares it
     * with its next block; the half-band is direct. */
    s->lanes = !halfband && channels > 1 ? 2 : 1;
    s->stages = (uint16_t)((channels + s->lanes - 1) / s->lanes);
    s->fir = (faar_dft_fir *)faar_alloc_zeroed(s->stages, sizeof(*s->fir));
    s->channel_input = (faar_sample_t *)AllocMemory(s->lanes * s->block_size
                                        * sizeof(faar_sample_t));
    s->channel_output_capacity = (channels == 1 && !halfband ? 2 : 1)
        * s->block_size / decimation;
    s->channel_output = (faar_sample_t *)AllocMemory(s->channel_output_capacity
                                         * channels * sizeof(faar_sample_t));
    if (!s->fir || !s->channel_input || !s->channel_output)
        goto fail;
    for (uint16_t f = 0; f < s->stages; ++f) {
        size_t lanes = channels - f * s->lanes < s->lanes
            ? channels - f * s->lanes : s->lanes;
        int stacked = channels == 1 && !halfband;
        int bad;
        if (f) {
            /* Every channel uses the same filter. Keep the first stage as
             * its owner and share FFT workspace between serialized channel stages. */
            bad = faar_dft_fir_init_shared(&s->fir[f], &s->fir[0], lanes, stacked);
        } else if (compensated) {
            bad = faar_dft_fir_init_compensated(&s->fir[f], taps, tap_count,
                           fft_size, expansion, decimation, lanes, stacked, halfband);
        } else if (halfband) {
            bad = faar_dft_fir_init_halfband(&s->fir[f], taps, tap_count);
        } else {
            bad = faar_dft_fir_init_paired(&s->fir[f], taps, tap_count,
                           fft_size, expansion, decimation, lanes, stacked);
        }
        if (bad) goto fail;
    }
    return s;
fail:
    faar_dft_prefilter_destroy(s);
    return NULL;
}

faar_dft_prefilter *faar_dft_prefilter_create_rate(uint16_t channels,
                                               const double *taps,
                                               size_t tap_count,
                                               size_t fft_size,
                                               size_t expansion,
                                               size_t decimation) {
    return faar_dft_prefilter_create_stage(channels, taps, tap_count, fft_size,
                                           expansion, decimation, 0, 0);
}

faar_dft_prefilter *faar_dft_prefilter_clone_filter(
        const faar_dft_prefilter *source) {
    if (!source || !source->fir || !source->stages || !source->channels
            || !source->lanes || !source->block_size
            || !source->channel_output_capacity)
        return NULL;
    faar_dft_prefilter *s = (faar_dft_prefilter *)faar_alloc_zeroed(1, sizeof(*s));
    if (!s) return NULL;
    s->channels = source->channels;
    s->expansion = source->expansion;
    s->decimation = source->decimation;
    s->half = source->half;
    s->block_size = source->block_size;
    s->lanes = source->lanes;
    s->stages = source->stages;
    s->channel_output_capacity = source->channel_output_capacity;
    s->prefix_drop = 2 * s->half / s->decimation;
    if (s->lanes > SIZE_MAX / s->block_size / sizeof(faar_sample_t)
            || s->channel_output_capacity > SIZE_MAX / s->channels
                    / sizeof(faar_sample_t))
        goto fail;
    s->fir = (faar_dft_fir *)faar_alloc_zeroed(s->stages, sizeof(*s->fir));
    s->channel_input = (faar_sample_t *)AllocMemory(s->lanes * s->block_size
                                            * sizeof(faar_sample_t));
    s->channel_output = (faar_sample_t *)AllocMemory(s->channel_output_capacity
            * s->channels * sizeof(faar_sample_t));
    if (!s->fir || !s->channel_input || !s->channel_output) goto fail;
    for (uint16_t f = 0; f < s->stages; ++f) {
        const faar_dft_fir *source_fir = &source->fir[f];
        int bad = f == 0
            ? faar_dft_fir_init_filter_shared(&s->fir[f], source_fir,
                    source_fir->lanes, source_fir->stacked)
            : faar_dft_fir_init_shared(&s->fir[f], &s->fir[0],
                    source_fir->lanes, source_fir->stacked);
        if (bad) goto fail;
    }
    return s;
fail:
    faar_dft_prefilter_destroy(s);
    return NULL;
}

void faar_dft_prefilter_release_workspace(faar_dft_prefilter *s) {
    if (!s) return;
    if (s->fir)
        for (uint16_t f = s->stages; f > 0; --f)
            faar_dft_fir_release_workspace(&s->fir[f - 1]);
    FreeMemory(s->channel_input);
    FreeMemory(s->channel_output);
    FreeMemory(s->fifo);
    FreeMemory(s->endpoint);
    s->channel_input = NULL;
    s->channel_output = NULL;
    s->fifo = NULL;
    s->endpoint = NULL;
    s->fifo_read = s->fifo_count = s->fifo_capacity = 0;
    s->endpoint_capacity = 0;
    s->prepared = 0;
    s->input_count = s->released_count = 0;
    s->have_input = s->prefix_added = 0;
    s->finishing = s->finished = 0;
    s->prefix_drop = 2 * s->half / s->decimation;
    memset(s->first, 0, sizeof(s->first));
    memset(s->last, 0, sizeof(s->last));
}

void faar_dft_prefilter_destroy(faar_dft_prefilter *s) {
    if (!s) return;
    if (s->fir) {
        /* Borrowers are destroyed before the stage owning their filter. */
        for (uint16_t f = s->stages; f > 0; --f)
            faar_dft_fir_clear(&s->fir[f - 1]);
    }
    FreeMemory(s->fir);
    FreeMemory(s->channel_input);
    FreeMemory(s->channel_output);
    FreeMemory(s->fifo);
    FreeMemory(s->endpoint);
    FreeMemory(s);
}

faar_status faar_dft_prefilter_prepare(faar_dft_prefilter *s,
        size_t max_input, size_t extra_output, size_t *output_bound) {
    if (!s || !output_bound || s->prefix_added || s->have_input
            || s->finishing || s->finished || s->half % s->expansion)
        return FAAR_ERR_INVALID_ARGUMENT;
    size_t prefix = s->half / s->expansion;
    size_t block = s->fir[0].stream_in;
    /* Normal calls can complete one retained partial block plus max_input.
     * At flush, the remaining centered output spans at most one block plus
     * the suffix: the prefix has already been removed by prefix_drop. These
     * are alternative bursts, not simultaneous ones. Reserve two extra output
     * frames for rounding and a cascade's final sampling-step span. */
    size_t extension = max_input > prefix ? max_input : prefix;
    if (extension > SIZE_MAX - block) return FAAR_ERR_INVALID_ARGUMENT;
    size_t frames = extension + block;
    if (frames > SIZE_MAX / s->expansion) return FAAR_ERR_INVALID_ARGUMENT;
    frames *= s->expansion;
    frames = frames / s->decimation + (frames % s->decimation != 0);
    if (frames > SIZE_MAX - 2 || extra_output > SIZE_MAX - frames - 2)
        return FAAR_ERR_INVALID_ARGUMENT;
    frames += extra_output + 2;
    if (frames > SIZE_MAX / s->channels / sizeof(float)
            || prefix > SIZE_MAX / s->channels / sizeof(float))
        return FAAR_ERR_INVALID_ARGUMENT;
    if (fifo_reserve(s, frames)) return FAAR_ERR_INSUFFICIENT_MEM;
    if (prefix > s->endpoint_capacity) {
        float *next = (float *)faar_grow_memory(s->endpoint,
                s->endpoint_capacity * s->channels * sizeof(float),
                prefix * s->channels * sizeof(float));
        if (!next) return FAAR_ERR_INSUFFICIENT_MEM;
        s->endpoint = next;
        s->endpoint_capacity = prefix;
    }
    memset(s->fifo, 0, s->fifo_capacity * s->channels * sizeof(float));
    if (s->endpoint)
        memset(s->endpoint, 0, s->endpoint_capacity * s->channels * sizeof(float));
    memset(s->channel_input, 0, s->lanes * s->block_size * sizeof(faar_sample_t));
    memset(s->channel_output, 0, s->channel_output_capacity * s->channels * sizeof(faar_sample_t));
    for (uint16_t f = 0; f < s->stages; ++f) faar_dft_fir_prepare(&s->fir[f]);
    *output_bound = frames;
    return FAAR_OK;
}

void faar_dft_prefilter_set_prepared(faar_dft_prefilter *s, int prepared) {
    if (s) s->prepared = prepared;
}

int faar_dft_prefilter_process(faar_dft_prefilter *s, const float *input,
                               size_t frames, float *output,
                               size_t output_capacity, size_t *produced) {
    if (!s || !produced || (frames && !input) || (output_capacity && !output)
            || s->finishing || s->finished) return -1;
    *produced = 0;
    if (frames) {
        for (size_t i = 0; i < frames * s->channels; ++i)
            if (!isfinite(input[i])) return -1;
        if (!s->prefix_added) {
            for (uint16_t c = 0; c < s->channels; ++c)
                s->first[c] = input[c];
            s->prefix_added = 1;
            if (s->half % s->expansion
                    || feed_endpoint(s, s->first,
                                     s->half / s->expansion)) return -1;
        }
        if (frames > SIZE_MAX / s->expansion - s->input_count) return -1;
        if (feed_raw(s, input, frames)) return -1;
        for (uint16_t c = 0; c < s->channels; ++c)
            s->last[c] = input[(frames - 1) * s->channels + c];
        s->input_count += frames;
        s->have_input = 1;
    }
    *produced = fifo_drain(s, output, output_capacity);
    return 0;
}

int faar_dft_prefilter_flush(faar_dft_prefilter *s, float *output,
                             size_t output_capacity, size_t *produced) {
    if (!s || !produced || (output_capacity && !output)) return -1;
    *produced = 0;
    if (!s->finishing && !s->finished) {
        s->finishing = 1;
        if (s->have_input) {
            if (s->half % s->expansion
                    || feed_endpoint(s, s->last,
                                     s->half / s->expansion)) return -1;
            /* A stacked stage may still hold a whole stacked block. */
            size_t capacity = s->channel_output_capacity;
            for (uint16_t f = 0; f < s->stages; ++f) {
                size_t channel_produced = 0;
                if (faar_dft_fir_flush(&s->fir[f],
                        s->channel_output + f * s->lanes * capacity, capacity,
                        &channel_produced)) return -1;
                if (f == 0) *produced = channel_produced;
                else if (*produced != channel_produced) return -1;
            }
            if (fifo_push_planes(s, s->channel_output, capacity, *produced))
                return -1;
            /* Prefix padding is removed by prefix_drop; suffix padding only
             * supplies the final centered FIR outputs. */
            if (s->released_count != (s->input_count * s->expansion
                    + s->decimation - 1) / s->decimation)
                return -1;
        }
        s->finished = 1;
        s->finishing = 0;
    }
    *produced = fifo_drain(s, output, output_capacity);
    return 0;
}

void faar_dft_prefilter_reset(faar_dft_prefilter *s) {
    if (!s) return;
    for (uint16_t f = 0; f < s->stages; ++f) faar_dft_fir_reset(&s->fir[f]);
    s->prefix_drop = 2 * s->half / s->decimation;
    s->fifo_read = s->fifo_count = 0;
    s->input_count = s->released_count = 0;
    s->have_input = s->prefix_added = 0;
    s->finishing = s->finished = 0;
    memset(s->first, 0, sizeof(s->first));
    memset(s->last, 0, sizeof(s->last));
}
