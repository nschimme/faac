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
    size_t input_count;
    size_t released_count;
    int have_input;
    int prefix_added;
    int hold;
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
    size_t capacity = s->fifo_capacity ? s->fifo_capacity : 4096;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2) { capacity = needed; break; }
        capacity *= 2;
    }
    if (capacity > SIZE_MAX / s->channels / sizeof(float)) return -1;
    float *next = (float *)malloc(capacity * s->channels * sizeof(float));
    if (!next) return -1;
    for (uint16_t c = 0; c < s->channels; ++c)
        if (s->fifo_count)
            memcpy(next + (size_t)c * capacity,
                   s->fifo + (size_t)c * s->fifo_capacity + s->fifo_read,
                   s->fifo_count * sizeof(float));
    free(s->fifo);
    s->fifo = next;
    s->fifo_capacity = capacity;
    s->fifo_read = 0;
    return 0;
}

static size_t fifo_drain(faar_dft_prefilter *s, float *output,
                         size_t capacity) {
    size_t n = s->fifo_count < capacity ? s->fifo_count : capacity;
    if (n)
        for (uint16_t c = 0; c < s->channels; ++c)
            memcpy(output + (size_t)c * n,
                   s->fifo + (size_t)c * s->fifo_capacity + s->fifo_read,
                   n * sizeof(float));
    s->fifo_read += n;
    s->fifo_count -= n;
    if (!s->fifo_count) s->fifo_read = 0;
    return n;
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
    float *block = (float *)malloc(frames * s->channels * sizeof(float));
    if (!block) return -1;
    for (size_t i = 0; i < frames; ++i)
        memcpy(block + i * s->channels, values,
               s->channels * sizeof(float));
    int result = feed_raw(s, block, frames);
    free(block);
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
                                               int halfband) {
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
    faar_dft_prefilter *s = (faar_dft_prefilter *)calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->channels = channels;
    s->expansion = expansion;
    s->decimation = decimation;
    s->half = (tap_count - 1) / 2;
    s->block_size = fft_size - tap_count + 1;
    if (s->half > SIZE_MAX / 2) { free(s); return NULL; }
    s->prefix_drop = 2 * s->half / decimation;
    s->hold = 1;
    /* Two channels share each complex transform, a lone channel shares it
     * with its next block; the half-band is direct. */
    s->lanes = !halfband && channels > 1 ? 2 : 1;
    s->stages = (uint16_t)((channels + s->lanes - 1) / s->lanes);
    s->fir = (faar_dft_fir *)calloc(s->stages, sizeof(*s->fir));
    s->channel_input = (faar_sample_t *)malloc(s->lanes * s->block_size
                                        * sizeof(faar_sample_t));
    s->channel_output_capacity = (channels == 1 && !halfband ? 2 : 1)
        * s->block_size / decimation;
    s->channel_output = (faar_sample_t *)malloc(s->channel_output_capacity
                                         * channels * sizeof(faar_sample_t));
    if (!s->fir || !s->channel_input || !s->channel_output)
        goto fail;
    for (uint16_t f = 0; f < s->stages; ++f) {
        if (s->lanes > 1) {
            size_t lanes = channels - f * s->lanes < s->lanes
                ? channels - f * s->lanes : s->lanes;
            if (faar_dft_fir_init_paired(&s->fir[f], taps, tap_count,
                    fft_size, expansion, decimation, lanes, 0)) goto fail;
            continue;
        }
        if (halfband ? faar_dft_fir_init_halfband(&s->fir[f], taps, tap_count)
                     : faar_dft_fir_init_paired(&s->fir[f], taps, tap_count,
                           fft_size, expansion, decimation, 1, 1))
            goto fail;
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
                                           expansion, decimation, 0);
}

void faar_dft_prefilter_destroy(faar_dft_prefilter *s) {
    if (!s) return;
    if (s->fir) {
        for (uint16_t f = 0; f < s->stages; ++f)
            faar_dft_fir_clear(&s->fir[f]);
    }
    free(s->fir);
    free(s->channel_input);
    free(s->channel_output);
    free(s->fifo);
    free(s);
}

int faar_dft_prefilter_set_boundary(faar_dft_prefilter *s, int hold) {
    if (!s || s->have_input || (hold != 0 && hold != 1)) return -1;
    s->hold = hold;
    return 0;
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
            if (s->hold) {
                if (s->half % s->expansion
                        || feed_endpoint(s, s->first,
                                         s->half / s->expansion)) return -1;
            } else {
                float zero[8] = {0};
                if (s->half % s->expansion
                        || feed_endpoint(s, zero,
                                         s->half / s->expansion)) return -1;
            }
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
            float zero[8] = {0};
            const float *endpoint = s->hold ? s->last : zero;
            if (s->half % s->expansion
                    || feed_endpoint(s, endpoint,
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
