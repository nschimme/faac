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

#include "faar.h"
#include "libfaar_internal.h"
#include "dft_coeffs.h"
#include "dft_prefilter.h"
#include "../common/atomic.h"
#include <limits.h>
#ifndef FAAR_WIDE_HIGH
#include "fft_float2.h"
#endif
#include <stdalign.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef MAX_CHANNELS
#define MAX_CHANNELS 8
#endif

#define POLYRESAMPLE_NUM_PHASES 64
#define POLYRESAMPLE_MAX_FILTER_LEN 512 /* Upper bound for stack-allocated temporary coefficient buffers */
#define POLYRESAMPLE_ALIGNMENT 64 /* 64-byte alignment for AVX-512 / AVX2 / NEON SIMD vector loads */

/* Over-allocate and align by hand so a custom AllocMemory needs no aligned variant. */
static void *aligned_malloc(size_t size) {
    if (size > SIZE_MAX - POLYRESAMPLE_ALIGNMENT - sizeof(void *)) return NULL;
    void *ptr = AllocMemory(size + POLYRESAMPLE_ALIGNMENT + sizeof(void *));
    if (!ptr) return NULL;
    uintptr_t addr = (uintptr_t)ptr + sizeof(void *) + POLYRESAMPLE_ALIGNMENT - 1;
    void *aligned_ptr = (void *)(addr & ~(uintptr_t)(POLYRESAMPLE_ALIGNMENT - 1));
    ((void **)aligned_ptr)[-1] = ptr;
    return aligned_ptr;
}

static void aligned_free(void *ptr) {
    if (!ptr) return;
    FreeMemory(((void **)ptr)[-1]);
}

static double bessel_i0(double x) {
    double sum = 1.0;
    double u = 1.0;
    double half_x = x / 2.0;
    for (int k = 1; k < 100; k++) {
        u *= (half_x / k) * (half_x / k);
        sum += u;
        if (u < sum * 1e-16) break;
    }
    return sum;
}

static double kaiser_window(double n, double N, double beta, double denominator) {
    if (beta <= 0.0) return 1.0;
    double arg = 2.0 * n / N - 1.0;
    double sq = 1.0 - arg * arg;
    if (sq <= 0.0) return 0.0;
    return bessel_i0(beta * sqrt(sq)) / denominator;
}

static uint32_t rate_gcd(uint32_t a, uint32_t b) {
    while (b) { uint32_t remainder = a % b; a = b; b = remainder; }
    return a;
}

struct faar_resampler_struct {
    uint32_t in_rate;
    uint32_t out_rate;
    uint16_t channels;
    faar_quality quality;
    uint32_t dither; /* 0, FAAR_DITHER_TPDF or FAAR_DITHER_SHAPED */
    uint8_t dither_depth;
    uint32_t prng_state[MAX_CHANNELS];
    float error_history[MAX_CHANNELS];

    size_t filter_len;
    size_t num_phases;
    double ratio;               /* out_rate / in_rate */
    uint64_t phase;             /* Input position in units of 1/out_rate */
    uint64_t input_frames;
    uint64_t output_frames;
    uint32_t phase_gcd;
    bool exact_phase_bank;
    bool half_sample_phase_bank;
    double beta;
    faar_sample_t *polyphase_table; /* Half-bank and interpolation guards, shared by all channels */
    faar_sample_t *polyphase_reverse_table; /* Reversed exact rows, avoids per-output coefficient copies */
#ifndef FAAR_WIDE_HIGH
    float *polyphase_low_table; /* Low words of High's filter coefficients. */
#endif
#ifdef FAAR_WIDE_HIGH
    double *polyphase_wide_table; /* Replaces polyphase_table for High. */
    double *polyphase_wide_reverse;
#endif

    faar_sample_t *hist[MAX_CHANNELS]; /* Keeps sample and coefficient arithmetic at the selected width */
    faar_sample_t last_input[MAX_CHANNELS]; /* Retained even when history is fully consumed */
    size_t hist_offset;         /* Frames before the moving history pointers */
    size_t hist_len;            /* Unconsumed frames in hist */
    size_t hist_cap_per_ch;     /* Allocated capacity per channel in frames */
    bool prepared;
    size_t prepared_max_input;

    size_t delay;
    size_t history_pad;
    bool flushed;               /* Tracks if flush zero-padding has already been appended */

    struct faar_resampler_struct *stage1;
    struct faar_resampler_struct *stage2;
    float *cascade_buf;
    size_t cascade_cap;
    faar_dft_prefilter *dft_prefilter;
    size_t dft_expansion;
    float *dft_intermediate;
    size_t dft_intermediate_capacity;
    faar_status last_error;
    bool shared_coeffs; /* Plan retains immutable banks for this stream. */
    faar_design *plan; /* Only the root of a plan-backed stream owns a reference. */
};

struct faar_design_struct {
    faac_once_t references; /* Atomic refcount, using the platform atomic type. */
    faar_resampler *prototype; /* Immutable geometry, coefficients and twiddles. */
    uint32_t max_input_frames; /* Per-stream reservation, 0 for none. */
};

static void plan_release(faar_design *plan);

static int plan_retain(faar_design *plan) {
#if !defined(__STDC_NO_ATOMICS__) && defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
    int count = atomic_load_explicit(&plan->references, memory_order_relaxed);
    while (count < INT_MAX) {
        if (atomic_compare_exchange_weak_explicit(&plan->references, &count,
                count + 1, memory_order_relaxed, memory_order_relaxed)) return 0;
    }
#elif defined(_MSC_VER)
    long count = _InterlockedCompareExchange(&plan->references, 0, 0);
    while (count < LONG_MAX) {
        long seen = _InterlockedCompareExchange(&plan->references, count + 1, count);
        if (seen == count) return 0;
        count = seen;
    }
#else
    if (plan->references < INT_MAX) { ++plan->references; return 0; }
#endif
    return -1;
}

static void clear_error(faar_resampler * r) {
    if (!r) return;
    r->last_error = FAAR_OK;
    clear_error(r->stage1);
    clear_error(r->stage2);
}

static faar_status find_error(const faar_resampler * r) {
    if (!r) return FAAR_OK;
    if (r->last_error != FAAR_OK) return r->last_error;
    faar_status st = find_error(r->stage1);
    return st != FAAR_OK ? st : find_error(r->stage2);
}

static size_t polyresample_process_single(faar_resampler * r, const float * restrict in_samples, size_t in_count,
                                           float * restrict out_samples, size_t max_out_count);

static size_t polyresample_flush_single(faar_resampler * r, float *out_samples, size_t max_out_count);

static size_t process_prefilter_output(faar_resampler *r, size_t capacity,
        float *out_samples, size_t max_out_count);

static int ensure_dft_intermediate(faar_resampler * r, size_t frames) {
    if (frames <= r->dft_intermediate_capacity) return 0;
    if (r->prepared) return -1;
    if (frames > SIZE_MAX / r->channels / sizeof(float)) return -1;
    float *next = (float *)faar_grow_memory(r->dft_intermediate,
            r->dft_intermediate_capacity * r->channels * sizeof(float),
            frames * r->channels * sizeof(float));
    if (!next) return -1;
    r->dft_intermediate = next;
    r->dft_intermediate_capacity = frames;
    return 0;
}

static faar_resampler * create_resampler_internal(uint32_t in_rate,
        uint32_t out_rate, uint16_t channels, faar_quality quality,
        uint32_t decimate_factor, size_t filter_len_override,
        bool force_polyphase, size_t max_phases);
static void destroy_internal(faar_resampler *r);
static faar_status open_stream(const faar_config *config, faar_resampler **out,
                               bool reserve);
static faar_status reserve_stream(faar_resampler *r, size_t max_input_frames);

/* Limit padding to the original input duration, without multiplying the
 * entire recording length by a rate. */
static size_t output_capacity(faar_resampler * r, size_t capacity) {
    if (r->flushed) {
        uint64_t target = (r->input_frames / r->in_rate) * r->out_rate
            + ((r->input_frames % r->in_rate) * r->out_rate + r->in_rate - 1) / r->in_rate;
        uint64_t remaining = target > r->output_frames ? target - r->output_frames : 0;
        if (remaining < capacity) capacity = (size_t)remaining;
    }
    return capacity;
}

/* Error feedback gives the noise transfer 1 - g*z^-1: -12 dB at DC and
 * +4.9 dB at Nyquist for g = 0.75, at the cost of 1 + g^2 (+1.9 dB) total
 * noise power. */
static const float shaping_gain = 0.75f;

/* 24-bit output quantizes 256 times finer than 16-bit. */
static const float lsb_ratio_24_to_16 = 1.0f / 256.0f;

/* Golden-ratio increment decorrelates the second xorshift32 stream used for
 * the triangular (difference of two uniforms) dither. */
static const uint32_t prng_stream_offset = 0x9e3779b9U;
static const float inv_2_32 = 1.0f / 4294967296.0f;

static void apply_dither(faar_resampler * r, float *samples, size_t frames) {
    if (!r->dither || !r->dither_depth) return;
    uint16_t c = 0;
    for (size_t i = 0; i < frames * r->channels; i++) {
        float sum = samples[i];
        float lsb_scale = (r->dither_depth == 16) ? 1.0f : lsb_ratio_24_to_16;
        float inv_lsb_scale = 1.0f / lsb_scale;

        uint32_t s1 = r->prng_state[c];
        s1 ^= s1 << 13; s1 ^= s1 >> 17; s1 ^= s1 << 5;
        r->prng_state[c] = s1;

        uint32_t s2 = s1 + prng_stream_offset;
        s2 ^= s2 << 13; s2 ^= s2 >> 17; s2 ^= s2 << 5;

        float r1 = (float)s1 * inv_2_32;
        float r2 = (float)s2 * inv_2_32;
        float tpdf = (r1 - r2) * lsb_scale;

        float sample_scaled = sum;
        if (r->dither == FAAR_DITHER_SHAPED) {
            sample_scaled -= r->error_history[c] * shaping_gain;
        }

        float quantized = (float)floorf((sample_scaled + tpdf) * inv_lsb_scale + 0.5f) * lsb_scale;
        r->error_history[c] = quantized - sample_scaled;
        sum = quantized;
        samples[i] = sum;
        if (++c == r->channels) c = 0;
    }
}

static bool short_half_rate_route(uint32_t in_rate, uint32_t out_rate) {
    return (uint64_t)in_rate == 2 * (uint64_t)out_rate
        && (in_rate == 44100 || in_rate == 48000 || in_rate == 88200
            || in_rate == 96000 || in_rate == 176400 || in_rate == 192000);
}

/* Band edges are fractions of the lower Nyquist frequency. The legacy
 * polyphase length and Kaiser beta apply when no spec-designed plan does. */
struct preset_spec {
    double passband, stopband, attenuation;
    uint16_t legacy_len;
    float legacy_beta;
};

static const struct preset_spec preset_specs[] = {
    /* Fast preserves 90% bandwidth with 100 dB alias rejection. */
    [FAAR_QUALITY_FAST] = {0.90, 1.04, 100.0, 127, 28.0f},
    /* Good preserves 96% bandwidth; float arithmetic limits measured rejection
     * to about 140 dB, so a 145 dB design is the most it can use. */
    [FAAR_QUALITY_GOOD] = {0.96, 1.04, 145.0, 255, 32.0f},
    [FAAR_QUALITY_HIGH] = {0.96, 1.04, 150.0, 511, 38.0f},
};

static const struct preset_spec *preset_spec(faar_quality quality) {
    return &preset_specs[quality <= FAAR_QUALITY_HIGH ? (size_t)quality
                                                      : FAAR_QUALITY_GOOD];
}

/* Smallest Kaiser margin that keeps every spec-designed filter above its
 * mask; 5.2 dB falls short at High. */
static const double kaiser_margin_db = 5.3;

/* Cascaded plans reserve extra rejection for the shorter final filter's
 * sidelobes and the added float boundary. */
static const double cascade_reserve_db = 4.7;

/* Kaiser's design formulas: taps = (A - kaiser_len_offset) /
 * (kaiser_len_slope * transition width), beta = kaiser_beta_slope *
 * (A - kaiser_beta_offset) for A > 50 dB of attenuation. */
static const double kaiser_len_offset = 7.95;
static const double kaiser_len_slope = 14.36;
static const double kaiser_beta_slope = 0.1102;
static const double kaiser_beta_offset = 8.7;

/* Legacy polyphase cutoffs in cycles per sample (0.5 is Nyquist): 96% of
 * Nyquist, or 98% when a caller supplies its own filter length. */
static const double legacy_cutoff = 0.48;
static const double legacy_cutoff_custom_len = 0.49;

/* Longest bank that stores a time-reversed copy of its exact rows. The
 * 41-tap upsampling banks repay the extra heap by skipping a per-output
 * coefficient reversal. */
static const size_t reverse_table_max_len = 64;

/* Below this |n| the sinc is evaluated at its limit 2*cutoff. */
static const double sinc_origin_eps = 1e-9;

static void design_band(const struct preset_spec *spec, double pb, double sb,
        size_t alignment, double reserve_db, size_t *tap_count, double *beta, double *cutoff) {
    /* Edges are cycles per input sample. */
    double attenuation = spec->attenuation + kaiser_margin_db + reserve_db;
    size_t n = (size_t)ceil((attenuation - kaiser_len_offset) / (kaiser_len_slope * (sb - pb))) + 1;
    *tap_count = ((n - 1 + alignment - 1) / alignment) * alignment + 1;
    *beta = kaiser_beta_slope * (attenuation - kaiser_beta_offset);
    *cutoff = 0.5 * (pb + sb);
}

/* Kaiser length and beta for a preset spec at filter_rate. alignment is the
 * divisor N-1 must meet so the DFT stage stays centred on input zero. */
static void design_from_spec(faar_quality quality, uint32_t out_rate,
        uint32_t filter_rate, double stopband, size_t alignment, double reserve_db,
        size_t *tap_count, double *beta, double *cutoff) {
    const struct preset_spec *spec = preset_spec(quality);
    double pb = spec->passband;
    double sb = stopband > 0.0 ? stopband : spec->stopband;
    design_band(spec, 0.5 * pb * out_rate / filter_rate,
                0.5 * sb * out_rate / filter_rate, alignment, reserve_db,
                tap_count, beta, cutoff);
}

struct short_plan {
    size_t taps, fft, expansion, decimation, bank;
    double beta, cutoff;
    size_t max_phases;
};

enum short_route { GENERAL, REFERENCE, UP, HALF, QUARTER, DOWN32 };

/* Prefilter designs. High keeps a measured fixed filter where taps is set;
 * every other preset, and High where taps is zero, designs from the preset
 * spec, taking the stop edge and FFT size from here for High when nonzero. */
enum short_design {
    SPEC, SPEC_HIGH_192_44, SPEC_HIGH_48_32,
    FIXED_HALF, FIXED_QUARTER, FIXED_REFERENCE, FIXED_UP, FIXED_UP_48_96
};

static const struct {
    double cutoff, beta, stop_edge;
    uint16_t taps, fft;
} short_designs[] = {
    [SPEC] = {0},
    [SPEC_HIGH_192_44] = {0, 0, 1.00045, 0, 32768},
    [SPEC_HIGH_48_32] = {0, 0, 1.00625, 0, 0},
    [FIXED_HALF] = {0.24889387661163931, 0, 0, 5493, 0},
    [FIXED_QUARTER] = {0.124411, 0, 0, 8193, 0},
    [FIXED_REFERENCE] = {0.22867124913694359, 0, 0, 5981, 0},
    [FIXED_UP] = {0.24889387661163931, 0, 0, 5493, 0},
    [FIXED_UP_48_96] = {0.24889387661163931, 18.401802232573989, 0, 5297, 0},
};

static const struct short_route_entry {
    uint32_t in, out;
    uint8_t route, expansion, bank, design;
} short_routes[] = {
    {48000, 44100, REFERENCE, 2, 20, FIXED_REFERENCE},
    {96000, 44100, REFERENCE, 1, 20, FIXED_REFERENCE},
    {192000, 44100, REFERENCE, 1, 20, SPEC_HIGH_192_44},
    {192000, 48000, QUARTER, 1, 1, FIXED_QUARTER},
    {44100, 32000, DOWN32, 2, 21, SPEC},
    {48000, 32000, DOWN32, 2, 1, SPEC_HIGH_48_32},
    {44100, 48000, UP, 2, 21, FIXED_UP},
    {44100, 88200, UP, 2, 1, FIXED_UP},
    {48000, 96000, UP, 2, 1, FIXED_UP_48_96},
    {96000, 192000, UP, 2, 1, FIXED_UP_48_96},
    {44100, 96000, UP, 2, 41, FIXED_UP},
    {48000, 88200, UP, 2, 41, FIXED_UP_48_96},
    /* Wider support suppresses phase modulation near input Nyquist. */
    {32000, 44100, UP, 2, 41, SPEC},
    {32000, 48000, UP, 2, 41, SPEC},
    {88200, 96000, UP, 2, 21, FIXED_UP},
};

static const struct short_route_entry *short_dedicated_route(uint32_t in,
        uint32_t out) {
    for (size_t i = 0; i < sizeof(short_routes) / sizeof(*short_routes); ++i) {
        if (in == short_routes[i].in && out == short_routes[i].out)
            return &short_routes[i];
    }
    return NULL;
}

/* Bank input must oversample the output by this factor: High's 0.96 Nyquist
 * residual brackets it in [2.48, 2.52], so take the centre. */
static const double bank_oversample = 2.50;

/* Upsampling without a dedicated entry designs its 2x prefilter from the
 * preset spec; this bank interpolates the doubled stream to the output. */
static const size_t general_up_bank = 41;

/* Fast and Good meet their masks with 21 taps of the sampled prototype on
 * every 2x upsampling bank; High keeps 41 for its rejection. */
static const size_t short_up_bank = 21;

/* Phase-table ceiling for that bank: rows beyond it are interpolated, so a
 * coprime rate pair stores 259 rows instead of up to 2051, for under 0.5 dB
 * of image rejection. */
static const size_t general_up_max_phases = 512;

static struct short_plan short_plan(uint32_t in, uint32_t out,
        faar_quality quality, bool general) {
    struct short_plan p = {0, 1, 1, 1, 21, 19.078143572239966, 0.0, 0};
    enum short_route route = short_half_rate_route(in, out) ? HALF : GENERAL;
    enum short_design design = route == HALF ? FIXED_HALF : SPEC;
    const struct short_route_entry *entry = short_dedicated_route(in, out);
    if (general) { route = GENERAL; design = SPEC; entry = NULL; }
    else if (!entry && in < out) {
        route = UP;
        p.expansion = 2;
        p.bank = (uint64_t)in * p.expansion == out ? 1
            : quality == FAAR_QUALITY_HIGH ? general_up_bank : short_up_bank;
        p.max_phases = general_up_max_phases;
    }
    if (entry) {
        route = entry->route;
        design = entry->design;
        p.expansion = entry->expansion;
        p.bank = entry->bank;
        if (route == UP && p.bank == 41 && quality != FAAR_QUALITY_HIGH)
            p.bank = short_up_bank;
    }
    switch (route) {
    case HALF:
        p.decimation = 2;
        p.bank = 1;
        break;
    case QUARTER:
        p.decimation = 4;
        break;
    case REFERENCE:
    case UP:
    case DOWN32:
        break;
    case GENERAL:
        p.expansion = 2;
        for (size_t d = 4; d; d /= 2) {
            for (size_t e = 1; e <= 2; ++e) {
                if ((uint64_t)in * e % d == 0
                        && (uint64_t)in * e >= bank_oversample * out * d) {
                    p.decimation = d;
                    p.expansion = e;
                    goto selected;
                }
            }
        }
selected:
        /* Near-unity ratios cannot reach K with 2x expansion; the even
         * prototype preserves their wider bank bandwidth. */
        if ((double)in * p.expansion / p.decimation / out < bank_oversample)
            p.bank = 20;
        if ((uint64_t)in * p.expansion % ((uint64_t)out * p.decimation) == 0)
            p.bank = 1;
        break;
    }
    const bool high = quality == FAAR_QUALITY_HIGH;
    if (high && short_designs[design].taps) {
        p.taps = short_designs[design].taps;
        p.cutoff = short_designs[design].cutoff;
        if (short_designs[design].beta) p.beta = short_designs[design].beta;
    } else {
        /* This stop edge also covers the near-Nyquist alias probe. */
        uint32_t lower = in < out ? in : out;
        double sb = high ? short_designs[design].stop_edge : 0.0;
        if (high && short_designs[design].fft) p.fft = short_designs[design].fft;
        size_t alignment = p.expansion == 2 ? 4 : p.decimation;
        if (alignment < 2) alignment = 2;
        design_from_spec(quality, lower, in * (uint32_t)p.expansion, sb,
                         alignment, general ? cascade_reserve_db : 0.0, &p.taps, &p.beta, &p.cutoff);
    }
    return p;
}

static faar_resampler * create_staged(uint32_t in_rate, uint32_t out_rate,
        uint16_t channels, faar_quality quality,
        const struct short_plan *selected) {
    struct short_plan plan = *selected;
    size_t tap_count = plan.taps;
    size_t fft_size = plan.fft ? plan.fft : 1;
    size_t expansion = plan.expansion;
    double beta = plan.beta;
    while (fft_size < tap_count * 5) {
        if (fft_size > SIZE_MAX / 2) return NULL;
        fft_size <<= 1;
    }
    double *taps = (double *)AllocMemory(tap_count * sizeof(*taps));
    if (!taps) return NULL;
    double cutoff = plan.cutoff;
    if (faar_dft_design_lowpass(taps, tap_count, cutoff, beta)) {
        FreeMemory(taps);
        return NULL;
    }
    if (expansion > 1) {
        for (size_t i = 0; i < tap_count; ++i)
            taps[i] *= (double)expansion;
    }
    struct faar_resampler_struct *r =
        (struct faar_resampler_struct *)faar_alloc_zeroed(1, sizeof(*r));
    if (!r) { FreeMemory(taps); return NULL; }
    r->in_rate = in_rate;
    r->out_rate = out_rate;
    r->channels = channels;
    r->quality = quality;
    r->ratio = (double)out_rate / (double)in_rate;
    r->dft_expansion = expansion;
    size_t decimation = plan.decimation;
    r->dft_prefilter = faar_dft_prefilter_create_stage(channels, taps,
            tap_count, fft_size, expansion, decimation, selected->fft == 0,
            quality == FAAR_QUALITY_HIGH);
    FreeMemory(taps);
    if (!r->dft_prefilter) { FreeMemory(r); return NULL; }
    size_t phase_bank_len = plan.bank;
    if ((uint64_t)in_rate * expansion > UINT32_MAX) {
        faar_dft_prefilter_destroy(r->dft_prefilter);
        FreeMemory(r);
        return NULL;
    }
    if (selected->fft == 0) {
        r->delay = (tap_count - 1) / 2;
        return r;
    }
    uint32_t stage2_rate = in_rate * (uint32_t)expansion
        / (uint32_t)decimation;
    r->stage2 = create_resampler_internal(stage2_rate, out_rate, channels,
                                         quality, 0, phase_bank_len, true,
                                         plan.max_phases);
    if (!r->stage2) {
        faar_dft_prefilter_destroy(r->dft_prefilter);
        FreeMemory(r);
        return NULL;
    }
    r->delay = ((tap_count - 1) / 2 + expansion * decimation - 1)
        / (expansion * decimation)
        + (r->stage2->delay + expansion * decimation - 1)
            / (expansion * decimation);
    return r;
}

/* The final bank must not exceed the GENERAL plan's rate or arithmetic cost;
 * the sharp filter moves to the lowest viable rate. */
static unsigned short_max_halvings(uint32_t in, uint32_t out, faar_quality q) {
    if (in <= out || short_half_rate_route(in, out)
            || short_dedicated_route(in, out)) return 0;
    struct short_plan original = short_plan(in, out, q, true);
    uint64_t bank = (uint64_t)in * original.expansion / original.decimation;
    unsigned k = 0;
    while (!(in & 1) && in / 2 > out) {
        struct short_plan next = short_plan(in / 2, out, q, true);
        uint64_t rate = (uint64_t)(in / 2) * next.expansion / next.decimation;
        if (rate < bank_oversample * out || rate > bank || next.bank > original.bank) break;
        in /= 2;
        ++k;
    }
    return k;
}

/* Non-negative least-squares fit of the relative cost terms. */
static const struct {
    double fir, fft, bank, visited;
} work_fit = {0.41445, 0.29928, 0.21198, 0.74921};

/* Estimate relative work from FIR operations, FFT size, bank length, and
 * input frames visited per output sample. */
static double short_work(uint32_t in, uint32_t out, faar_quality quality,
        unsigned k, double fir_work, double visited) {
    struct short_plan p = short_plan(in, out, quality, k != 0);
    size_t n = p.fft;
    while (n < p.taps * 5) n *= 2;
    double forward = (double)n / p.expansion;
    double inverse = (double)n / p.decimation;
    double block_input = (double)(n - p.taps + 1) / p.expansion;
    double fft_work = (forward * log2(forward) + inverse * log2(inverse))
        / block_input * in / out;
    return work_fit.fir * fir_work + work_fit.fft * fft_work
        + work_fit.bank * p.bank
        + work_fit.visited * (visited + (double)in / out);
}

static struct short_plan short_preplan(uint32_t in, uint32_t out,
        faar_quality quality) {
    struct short_plan pre = {0, 0, 1, 2, 1, 0, 0, 0};
    /* Symmetric edges cover the preset pass edge and reject aliases before
     * they can land inside 1.04 times FINAL output Nyquist. */
    double pb = 0.52 * out / in;
    design_band(preset_spec(quality), pb, 0.5 - pb, 4, cascade_reserve_db,
                &pre.taps, &pre.beta, &pre.cutoff);
    return pre;
}

static unsigned short_halvings(uint32_t in, uint32_t out, faar_quality q,
        const struct short_route_entry *entry) {
    bool reference = entry && entry->route == REFERENCE;
    /* On these routes sparse half-bands down to below 4:1 measured cheaper
       than the general cost model's choice, at the same quality. */
    bool prefer = (q == FAAR_QUALITY_GOOD
            && ((!entry && in % out == 0) || reference))
        || (q == FAAR_QUALITY_HIGH && reference);
    if (prefer && (uint64_t)in >= 4 * (uint64_t)out) {
        unsigned count = 0;
        while (!(in & 1) && (uint64_t)in >= 4 * (uint64_t)out) {
            in /= 2;
            ++count;
        }
        return count;
    }
    unsigned limit = short_max_halvings(in, out, q), best = 0;
    if (!limit) return 0;
    double fir = 0, visited = 0;
    double minimum = short_work(in, out, q, 0, 0, 0);
    for (unsigned k = 1; k <= limit; ++k) {
        struct short_plan pre = short_preplan(in, out, q);
        /* Paired odd coefficients plus the centre, per FINAL output. */
        fir += (double)in / (2.0 * out) * (pre.taps / 4 + 1);
        visited += (double)in / out;
        in /= 2;
        double work = short_work(in, out, q, k, fir, visited);
        if (work < minimum) { minimum = work; best = k; }
    }
    return best;
}

static faar_resampler * short_cascade(uint32_t in, uint32_t out,
        uint16_t channels, faar_quality quality, unsigned k, bool dedicated) {
    if (!k) {
        struct short_plan p = short_plan(in, out, quality, !dedicated);
        return create_staged(in, out, channels, quality, &p);
    }
    struct short_plan pre = short_preplan(in, out, quality);
    faar_resampler * r = faar_alloc_zeroed(1, sizeof(*r));
    if (!r) return NULL;
    r->in_rate = in;
    r->out_rate = out;
    r->channels = channels;
    r->quality = quality;
    r->ratio = (double)out / in;
    r->stage1 = create_staged(in, in / 2, channels, quality, &pre);
    r->stage2 = short_cascade(in / 2, out, channels, quality, k - 1, dedicated);
    r->cascade_cap = 4096 * channels;
    r->cascade_buf = aligned_malloc(r->cascade_cap * sizeof(float));
    if (!r->stage1 || !r->stage2 || !r->cascade_buf) {
        destroy_internal(r);
        return NULL;
    }
    r->delay = r->stage1->delay + 2 * r->stage2->delay;
    return r;
}

static void store_coefficient(faar_resampler *r, size_t at, double val) {
#ifdef FAAR_WIDE_HIGH
    if (r->polyphase_wide_table) {
        r->polyphase_wide_table[at] = val;
        return;
    }
#endif
    r->polyphase_table[at] = (faar_sample_t)val;
#ifndef FAAR_WIDE_HIGH
    if (r->polyphase_low_table)
        r->polyphase_low_table[at] = (float)(val - (double)r->polyphase_table[at]);
#endif
}

static faar_resampler * create_resampler_internal(uint32_t in_rate, uint32_t out_rate,
        uint16_t channels, faar_quality quality, uint32_t decimate_factor,
        size_t filter_len_override, bool force_polyphase, size_t max_phases) {
    if (in_rate == 0 || out_rate == 0 || channels == 0 || channels > MAX_CHANNELS) return NULL;

    const struct short_route_entry *entry = short_dedicated_route(in_rate, out_rate);
    bool use_staged = in_rate != out_rate || entry;
    /* The final phase bank must bypass planning to avoid recursive stages. */
    if (!force_polyphase && use_staged && decimate_factor < 2) {
        unsigned k = short_halvings(in_rate, out_rate, quality, entry);
        if (k) return short_cascade(in_rate, out_rate, channels, quality, k,
            quality == FAAR_QUALITY_GOOD || entry);
        struct short_plan p = short_plan(in_rate, out_rate, quality, false);
        return create_staged(in_rate, out_rate, channels, quality, &p);
    }

    bool force_decimate = (decimate_factor >= 2) || ((uint64_t)in_rate > 2 * (uint64_t)out_rate);

    if (force_decimate && in_rate > out_rate && !filter_len_override) {
        uint32_t intermediate_rate = in_rate / (decimate_factor >= 2 ? decimate_factor : 2);
        if (intermediate_rate <= out_rate) intermediate_rate = in_rate / 2;
        if (intermediate_rate <= out_rate) intermediate_rate = (in_rate + out_rate) / 2;

        struct faar_resampler_struct *r = (struct faar_resampler_struct *)faar_alloc_zeroed(1, sizeof(struct faar_resampler_struct));
        if (!r) return NULL;

        r->stage1 = create_resampler_internal(in_rate, intermediate_rate,
                channels, quality, 0, 0, true, 0);
        r->stage2 = create_resampler_internal(intermediate_rate, out_rate,
                channels, quality, 0, 0, true, 0);

        if (!r->stage1 || !r->stage2) {
            destroy_internal(r->stage1);
            destroy_internal(r->stage2);
            FreeMemory(r);
            return NULL;
        }

        r->in_rate = in_rate;
        r->out_rate = out_rate;
        r->channels = channels;
        r->quality = quality;
        r->ratio = (double)out_rate / (double)in_rate;

        r->cascade_cap = 4096 * channels;
        r->cascade_buf = (float *)aligned_malloc(r->cascade_cap * sizeof(float));
        if (!r->cascade_buf) {
            destroy_internal(r->stage1);
            destroy_internal(r->stage2);
            FreeMemory(r);
            return NULL;
        }

        r->delay = r->stage1->delay
            + ((uint64_t)r->stage2->delay * in_rate + intermediate_rate - 1) / intermediate_rate;
        return r;
    }

    struct faar_resampler_struct *r = (struct faar_resampler_struct *)faar_alloc_zeroed(1, sizeof(struct faar_resampler_struct));
    if (!r) return NULL;

    r->in_rate = in_rate;
    r->out_rate = out_rate;
    r->channels = channels;
    r->quality = quality;
    r->ratio = (double)out_rate / (double)in_rate;

    /* Preset lengths and Kaiser beta; rejection depends on the rate and frequency. */
    r->filter_len = preset_spec(quality)->legacy_len;
    r->beta = preset_spec(quality)->legacy_beta;

    if (filter_len_override == 20 || filter_len_override == 21
            || filter_len_override == 41)
        r->beta = 19.60632601597565;

    if (filter_len_override) {
        r->filter_len = filter_len_override;
        size_t exact_phase_count = 2u * (size_t)(out_rate / rate_gcd(in_rate, out_rate));
        if (!max_phases) max_phases = 4096;
        r->num_phases = exact_phase_count <= max_phases ? exact_phase_count : max_phases;
        r->phase_gcd = rate_gcd(in_rate, out_rate);
        r->exact_phase_bank = exact_phase_count <= max_phases;
        /* The even 20-tap bank needs a half-sample clock origin. */
        if (filter_len_override == 20)
            r->half_sample_phase_bank = true;
    } else r->num_phases = POLYRESAMPLE_NUM_PHASES;

    if ((r->filter_len % 2) == 0
            && filter_len_override != 20) r->filter_len++;
    assert(r->filter_len <= POLYRESAMPLE_MAX_FILTER_LEN);

    size_t stored_phases = r->num_phases / 2 + 3;
#ifdef FAAR_WIDE_HIGH
    if (quality == FAAR_QUALITY_HIGH) {
        r->polyphase_wide_table = aligned_malloc(stored_phases * r->filter_len
                * sizeof(*r->polyphase_wide_table));
        if (!r->polyphase_wide_table) {
            FreeMemory(r);
            return NULL;
        }
    } else
#endif
    {
        r->polyphase_table = (faar_sample_t *)aligned_malloc(stored_phases * r->filter_len * sizeof(*r->polyphase_table));
        if (!r->polyphase_table) {
            FreeMemory(r);
            return NULL;
        }
    }
    /* The short exact banks (up to the 41-tap upsampling banks) repay a
     * second oriented copy; keep generic banks compact and let their
     * uncommon exact reverse phase use the stack path. */
#ifndef FAAR_WIDE_HIGH
    if (quality == FAAR_QUALITY_HIGH) {
        size_t bytes = stored_phases * r->filter_len * sizeof(float);
        r->polyphase_low_table = aligned_malloc(bytes);
        if (!r->polyphase_low_table) { destroy_internal(r); return NULL; }
        memset(r->polyphase_low_table, 0, bytes);
    }
#endif
    if (r->exact_phase_bank && r->filter_len <= reverse_table_max_len
#ifndef FAAR_WIDE_HIGH
            && !r->polyphase_low_table
#endif
            ) {
#ifdef FAAR_WIDE_HIGH
        if (r->polyphase_wide_table) {
            r->polyphase_wide_reverse = aligned_malloc(stored_phases * r->filter_len
                    * sizeof(*r->polyphase_wide_reverse));
            if (!r->polyphase_wide_reverse) {
                destroy_internal(r);
                return NULL;
            }
        } else
#endif
        {
            r->polyphase_reverse_table = (faar_sample_t *)aligned_malloc(
                    stored_phases * r->filter_len * sizeof(*r->polyphase_reverse_table));
            if (!r->polyphase_reverse_table) {
                destroy_internal(r);
                return NULL;
            }
        }
    }

    double cutoff = (r->ratio < 1.0)
        ? (filter_len_override ? legacy_cutoff_custom_len : legacy_cutoff * r->ratio)
        : legacy_cutoff;
    double half_len = (r->filter_len - 1) / 2.0;
    const bool long_up = filter_len_override == 21
        && (uint64_t)r->out_rate * 2 > r->in_rate && r->num_phases >= 2;
    const bool sampled_long_prototype = filter_len_override == 20 || long_up;
    const double window_denominator = r->beta <= 0.0 ? 1.0 : bessel_i0(r->beta);
    double window[POLYRESAMPLE_MAX_FILTER_LEN];
    if (r->filter_len > 1 && !sampled_long_prototype) {
        for (size_t j = 0; j < r->filter_len; j++)
            window[j] = kaiser_window((double)j,
                                      (double)r->filter_len - 1.0, r->beta,
                                      window_denominator);
    }

    /* Store phases -1..N/2+1, including guards for cubic interpolation. */
    for (size_t p = 0; p < stored_phases; p++) {
        double frac = ((double)p - 1.0) / (double)r->num_phases;
        faar_sample_t *phase_coeffs = r->polyphase_table
            ? r->polyphase_table + p * r->filter_len : NULL;
        double sum = 0.0;

        for (size_t j = 0; j < r->filter_len; j++) {
            if (r->filter_len == 1) {
                store_coefficient(r, p * r->filter_len + j, 1.0);
                sum = 1.0;
                continue;
            }
            if (sampled_long_prototype) {
                /* Rows are samples of one long prototype; windowing each
                 * as its own FIR would shift the even phase origin. */
                const bool bank_147 = filter_len_override == 20;
                const double up_phases = (double)(r->num_phases / 2);
                const double prototype_fc = bank_147
                    ? 0.006075233237773342 : 0.89741699260155768 / up_phases;
                const double prototype_phases = bank_147 ? 147.0 : up_phases;
                const double prototype_center = bank_147 ? 1469.0
                    : (double)r->filter_len * up_phases / 2 - 1;
                const double prototype_denominator = prototype_center + 0.75;
                const double prototype_scale = prototype_phases;
                double phase_count = bank_147 ? 294.0 : (double)r->num_phases;
                double prototype_index =
                    ((double)(r->filter_len - 1 - j) * prototype_phases)
                    + (frac + 0.5) * phase_count * 0.5 - 1.0;
                double z = prototype_index - prototype_center;
                double y = z / prototype_denominator;
                double radicand = 1.0 - y * y;
                double sinc = fabs(z) < 1e-14 ? prototype_fc
                    : sin(M_PI * prototype_fc * z) / (M_PI * z);
                double win = radicand <= 0.0 ? 0.0
                    : bessel_i0(r->beta * sqrt(radicand)) / window_denominator;
                double val = sinc * win * prototype_scale;
                store_coefficient(r, p * r->filter_len + j, val);
                sum += val;
                continue;
            }
            double n = (double)j - half_len - frac;
            double sinc = (fabs(n) < sinc_origin_eps) ? (2.0 * cutoff) : sin(2.0 * M_PI * cutoff * n) / (M_PI * n);
            double win = window[j];
            double val = sinc * win;
            store_coefficient(r, p * r->filter_len + j, val);
            sum += val;
        }

        bool normalize_phase = true;
        if (sampled_long_prototype)
            normalize_phase = false;
#ifdef FAAR_WIDE_HIGH
        if (normalize_phase && sum > 0.0 && r->polyphase_wide_table) {
            double *wide = r->polyphase_wide_table + p * r->filter_len;
            for (size_t j = 0; j < r->filter_len; j++)
                wide[j] = wide[j] / sum;
        } else
#endif
        if (normalize_phase && sum > 0.0) {
            for (size_t j = 0; j < r->filter_len; j++) {
#ifdef FAAR_WIDE_HIGH
                phase_coeffs[j] /= (float)sum;
#else
                if (r->polyphase_low_table) {
                    size_t at = p * r->filter_len + j;
                    double val = ((double)phase_coeffs[j] + r->polyphase_low_table[at]) / sum;
                    phase_coeffs[j] = (float)val;
                    r->polyphase_low_table[at] = (float)(val - (double)phase_coeffs[j]);
                } else {
                    phase_coeffs[j] /= (float)sum;
                }
#endif
            }
        }
    }
    if (r->polyphase_reverse_table) {
        for (size_t p = 0; p < stored_phases; ++p) {
            const faar_sample_t *source =
                r->polyphase_table + p * r->filter_len;
            faar_sample_t *reverse =
                r->polyphase_reverse_table + p * r->filter_len;
            for (size_t j = 0; j < r->filter_len; ++j)
                reverse[j] = source[r->filter_len - 1 - j];
        }
    }
#ifdef FAAR_WIDE_HIGH
    if (r->polyphase_wide_reverse) {
        for (size_t p = 0; p < stored_phases; ++p) {
            const double *source = r->polyphase_wide_table + p * r->filter_len;
            double *reverse = r->polyphase_wide_reverse + p * r->filter_len;
            for (size_t j = 0; j < r->filter_len; ++j)
                reverse[j] = source[r->filter_len - 1 - j];
        }
    }
#endif

    /* Keep each planar history aligned to 64 bytes for vector-friendly strides. */
    r->hist_cap_per_ch = (r->filter_len * 2 + 4096 + 15) & ~((size_t)15);

    for (uint16_t c = 0; c < channels; c++) {
        r->hist[c] = (faar_sample_t *)aligned_malloc(
                r->hist_cap_per_ch * sizeof(*r->hist[c]));
        if (!r->hist[c]) {
            destroy_internal(r);
            return NULL;
        }
        memset(r->hist[c], 0, r->hist_cap_per_ch * sizeof(*r->hist[c]));
    }

    r->delay = in_rate == out_rate ? 0 : (size_t)half_len;
    /* Center the first output on input frame zero. */
    r->history_pad = r->delay;
    if (filter_len_override == 20) {
        r->history_pad++;
        r->phase = out_rate / 2;
    }
    r->hist_len = in_rate == out_rate ? 0 : r->history_pad;
    return r;
}

/* Map the public flag bits to the internal mode and depth; zero means none. */
static int dither_from_flags(uint32_t flags, uint32_t *dither, uint8_t *bits) {
    const uint32_t mode_mask = FAAR_DITHER_TPDF | FAAR_DITHER_SHAPED;
    const uint32_t depth_mask = FAAR_DITHER_16 | FAAR_DITHER_24;
    if (flags & ~(mode_mask | depth_mask)) return -1;
    if (flags == 0) { *dither = 0; *bits = 0; return 0; }
    uint32_t mode = flags & mode_mask;
    uint32_t depth = flags & depth_mask;
    if ((mode != FAAR_DITHER_TPDF && mode != FAAR_DITHER_SHAPED)
            || (depth != FAAR_DITHER_16 && depth != FAAR_DITHER_24))
        return -1;
    *dither = mode;
    *bits = (uint8_t)(depth == FAAR_DITHER_16 ? 16 : 24);
    return 0;
}

static int set_dither_internal(faar_resampler * r, uint32_t dither, uint8_t target_bits) {
    if (!r) return -1;
    if (dither != 0 && dither != FAAR_DITHER_TPDF
            && dither != FAAR_DITHER_SHAPED) return -1;
    if (dither != 0 && target_bits != 16 && target_bits != 24) return -1;

    r->dither = dither;
    r->dither_depth = target_bits;
    if (r->stage2) {
        return set_dither_internal(r->stage2, dither, target_bits);
    }
    for (uint16_t c = 0; c < r->channels && c < MAX_CHANNELS; c++) {
        r->prng_state[c] = (uint32_t)(123456789U + c * 987654321U);
        r->error_history[c] = 0.0f;
    }
    return 0;
}

/* Phase offsets below this are treated as grid-exact: the interpolation
 * correction would shift the evaluated position by under 1e-6 of a row
 * spacing, far below the stopband floor. */
static const faar_sample_t exact_phase_eps = (faar_sample_t)1e-6;

#include "polyresample_float2.inc"
#include "polyresample_wide.inc"

/* Keep history contiguous for the FIR, but move it only when an append
 * exhausts the trailing space. Draining output never copies queued audio. */
static int reserve_history(faar_resampler *r, size_t additional, bool exact) {
    if (additional > SIZE_MAX - r->hist_len) {
        r->last_error = FAAR_ERR_INVALID_ARGUMENT;
        return -1;
    }
    size_t needed = r->hist_len + additional;
    if (needed <= r->hist_cap_per_ch - r->hist_offset) return 0;
    if (r->hist_offset) {
        for (uint16_t c = 0; c < r->channels; ++c) {
            faar_sample_t *base = r->hist[c] - r->hist_offset;
            memmove(base, r->hist[c], r->hist_len * sizeof(*base));
            r->hist[c] = base;
        }
        r->hist_offset = 0;
    }
    if (needed <= r->hist_cap_per_ch) return 0;
    if (r->prepared) {
        r->last_error = FAAR_ERR_NO_MEMORY;
        return -1;
    }
    size_t growth = exact ? 1 : 2;
    if (needed > (SIZE_MAX - 15) / growth) {
        r->last_error = FAAR_ERR_INVALID_ARGUMENT;
        return -1;
    }
    size_t capacity = (needed * growth + 15) & ~((size_t)15);
    if (capacity > SIZE_MAX / sizeof(faar_sample_t)) {
        r->last_error = FAAR_ERR_INVALID_ARGUMENT;
        return -1;
    }
    /* All pointers now have the same origin. Grow one channel at a time to
     * limit peak memory; on failure every channel still has at least the old
     * capacity, and destruction remains safe. */
    for (uint16_t c = 0; c < r->channels; ++c) {
        faar_sample_t *next = aligned_malloc(capacity * sizeof(*next));
        if (!next) {
            r->last_error = FAAR_ERR_NO_MEMORY;
            return -1;
        }
        memcpy(next, r->hist[c], r->hist_len * sizeof(*next));
        aligned_free(r->hist[c]);
        r->hist[c] = next;
    }
    r->hist_cap_per_ch = capacity;
    return 0;
}

static void set_prepared(faar_resampler *r, bool prepared) {
    if (!r) return;
    r->prepared = prepared;
    faar_dft_prefilter_set_prepared(r->dft_prefilter, prepared);
    set_prepared(r->stage1, prepared);
    set_prepared(r->stage2, prepared);
}

/* Ceil(frames * numerator / denominator), without a size_t product overflow. */
static int scaled_frames(size_t frames, uint32_t numerator, uint32_t denominator,
                         size_t *out) {
    size_t whole = frames / denominator;
    uint64_t tail = (uint64_t)(frames % denominator) * numerator;
    uint64_t extra = tail / denominator + (tail % denominator != 0);
    if (whole > SIZE_MAX / numerator || extra > SIZE_MAX - whole * numerator)
        return -1;
    *out = whole * numerator + (size_t)extra;
    return 0;
}

/* Burst bounds include retained filter support, transform blocks and flush
 * padding. A downstream stage may receive an entire upstream burst before
 * the caller's small output buffer has drained it. */
static faar_status prepare_internal(faar_resampler *r, size_t max_input,
                                    size_t *output_bound) {
    if (r->dft_prefilter) {
        if (max_input > (SIZE_MAX - 8192) / r->dft_expansion)
            return FAAR_ERR_INVALID_ARGUMENT;
        size_t intermediate = max_input * r->dft_expansion + 8192;
        if (intermediate > SIZE_MAX / r->channels / sizeof(float))
            return FAAR_ERR_INVALID_ARGUMENT;
        if (!r->stage2) {
            if (ensure_dft_intermediate(r, intermediate)) return FAAR_ERR_NO_MEMORY;
            memset(r->dft_intermediate, 0,
                   r->dft_intermediate_capacity * r->channels * sizeof(float));
        }
        size_t step = r->stage2
            ? r->stage2->in_rate / r->stage2->out_rate
                + (r->stage2->in_rate % r->stage2->out_rate != 0) : 0;
        size_t filtered;
        faar_status st = faar_dft_prefilter_prepare(r->dft_prefilter,
                max_input, step, &filtered);
        if (st != FAAR_OK) return st;
        if (r->stage2) return prepare_internal(r->stage2, filtered, output_bound);
        *output_bound = filtered;
        return FAAR_OK;
    }
    if (r->stage1 && r->stage2) {
        memset(r->cascade_buf, 0, r->cascade_cap * sizeof(float));
        size_t middle;
        faar_status st = prepare_internal(r->stage1, max_input, &middle);
        return st == FAAR_OK ? prepare_internal(r->stage2, middle, output_bound) : st;
    }
    /* Retained support, a pending discard, and the final boundary tail. */
    size_t support = 3 * r->filter_len;
    if (max_input > SIZE_MAX - support) return FAAR_ERR_INVALID_ARGUMENT;
    size_t frames = max_input + support;
    if (reserve_history(r, frames, true)) return r->last_error;
    for (uint16_t c = 0; c < r->channels; ++c)
        memset(r->hist[c], 0, r->hist_cap_per_ch * sizeof(faar_sample_t));
    if (scaled_frames(frames, r->out_rate, r->in_rate, output_bound))
        return FAAR_ERR_INVALID_ARGUMENT;
    return FAAR_OK;
}

static size_t polyresample_process_single(faar_resampler * restrict r, const float * restrict in_samples, size_t in_count,
                                           float * restrict out_samples, size_t max_out_count) {
    if (!r || !out_samples) return 0;

    const size_t filter_len = r->filter_len;
    const size_t half_len = (filter_len - 1) / 2;
    const faar_sample_t * restrict poly_table = r->polyphase_table
        ? r->polyphase_table + filter_len : NULL;

    assert(filter_len <= POLYRESAMPLE_MAX_FILTER_LEN);

    if (in_samples && in_count > 0) {
        if (reserve_history(r, in_count, false)) return 0;

        if (r->channels == 2) {
            faar_sample_t * restrict h0 = r->hist[0] + r->hist_len;
            faar_sample_t * restrict h1 = r->hist[1] + r->hist_len;
            for (size_t i = 0; i < in_count; i++) {
                h0[i] = (faar_sample_t)in_samples[i * 2 + 0];
                h1[i] = (faar_sample_t)in_samples[i * 2 + 1];
            }
        } else if (r->channels == 6) {
            faar_sample_t * restrict h0 = r->hist[0] + r->hist_len;
            faar_sample_t * restrict h1 = r->hist[1] + r->hist_len;
            faar_sample_t * restrict h2 = r->hist[2] + r->hist_len;
            faar_sample_t * restrict h3 = r->hist[3] + r->hist_len;
            faar_sample_t * restrict h4 = r->hist[4] + r->hist_len;
            faar_sample_t * restrict h5 = r->hist[5] + r->hist_len;
            for (size_t i = 0; i < in_count; i++) {
                h0[i] = (faar_sample_t)in_samples[i * 6 + 0];
                h1[i] = (faar_sample_t)in_samples[i * 6 + 1];
                h2[i] = (faar_sample_t)in_samples[i * 6 + 2];
                h3[i] = (faar_sample_t)in_samples[i * 6 + 3];
                h4[i] = (faar_sample_t)in_samples[i * 6 + 4];
                h5[i] = (faar_sample_t)in_samples[i * 6 + 5];
            }
        } else if (r->channels == 1) {
            for (size_t i = 0; i < in_count; ++i)
                r->hist[0][r->hist_len + i] = (faar_sample_t)in_samples[i];
        } else {
            for (size_t i = 0; i < in_count; i++) {
                for (uint16_t c = 0; c < r->channels; c++) {
                    r->hist[c][r->hist_len + i] =
                        (faar_sample_t)in_samples[i * r->channels + c];
                }
            }
        }
        /* Initialize the virtual past only once, independently of chunk size. */
        if (!r->input_frames && !r->flushed) {
            for (uint16_t c = 0; c < r->channels; c++) {
            faar_sample_t first = r->hist[c][r->history_pad];
                for (size_t j = 0; j < r->history_pad; j++) r->hist[c][j] = first;
            }
        }
        r->hist_len += in_count;
        if (!r->flushed) {
            for (uint16_t c = 0; c < r->channels; c++)
                r->last_input[c] = r->hist[c][r->hist_len - 1];
            r->input_frames += in_count;
        }
    }

    max_out_count = output_capacity(r, max_out_count);
    if (r->in_rate == r->out_rate) {
        size_t frames = r->hist_len < max_out_count ? r->hist_len : max_out_count;
        for (uint16_t c = 0; c < r->channels; c++) {
            for (size_t j = 0; j < frames; j++) {
                out_samples[j * r->channels + c] = (float)r->hist[c][j];
            }
            r->hist[c] += frames;
        }
        r->hist_offset += frames;
        r->hist_len -= frames;
        r->output_frames += frames;
        apply_dither(r, out_samples, frames);
        return frames;
    }

    size_t out_frames = 0;
    /* Split the rational clock once per call. Updating quotient and remainder
     * per output avoids a 64-bit divide/modulo pair in the sample loop. */
    size_t base_idx = (size_t)(r->phase / r->out_rate);
    uint64_t phase_remainder = r->phase % r->out_rate;
    const uint64_t step_whole = r->in_rate / r->out_rate;
    const uint64_t step_remainder = r->in_rate % r->out_rate;
    uint64_t phase_slot = r->exact_phase_bank
        ? (r->half_sample_phase_bank
            ? (2 * phase_remainder) / r->phase_gcd
            : phase_remainder / r->phase_gcd) : 0;
    const uint64_t phase_slot_step = r->exact_phase_bank
        ? (r->half_sample_phase_bank
            ? (2 * step_remainder) / r->phase_gcd
            : step_remainder / r->phase_gcd) : 0;
    const uint64_t phase_slot_modulus = r->exact_phase_bank
        ? (r->half_sample_phase_bank
            ? (2 * r->out_rate) / r->phase_gcd
            : r->out_rate / r->phase_gcd) : 0;

#if defined(_MSC_VER)
    __declspec(align(64)) faar_sample_t temp_coeffs[POLYRESAMPLE_MAX_FILTER_LEN];
#else
    alignas(64) faar_sample_t temp_coeffs[POLYRESAMPLE_MAX_FILTER_LEN];
#endif

    while (out_frames < max_out_count) {
        size_t sample_base_idx = base_idx;
        faar_sample_t frac = 0;
        bool reverse;
        size_t p_idx;
        faar_sample_t delta;
#ifndef FAAR_WIDE_HIGH
        faar_float2 precise_delta = ff2(0);
#endif
#ifdef FAAR_WIDE_HIGH
        double wide_delta = 0;
#endif
        if (r->exact_phase_bank) {
            /* The doubled grid represents the 20-tap bank's half-sample
                 * origin; all other exact banks use the ordinary reduced grid. */
            uint64_t mapped = phase_slot;
            reverse = mapped * 2 > phase_slot_modulus;
            if (reverse) {
                sample_base_idx++;
                mapped = phase_slot_modulus - mapped;
            }
            uint64_t phase_index = r->half_sample_phase_bank
                ? mapped : 2 * mapped;
            if (phase_index < r->num_phases / 2) {
                p_idx = (size_t)phase_index;
                delta = 0;
            } else {
                p_idx = r->num_phases / 2 - 1;
                delta = 1;
            }
        }
#ifndef FAAR_WIDE_HIGH
        else if (r->polyphase_low_table) {
            reverse = 2 * phase_remainder > r->out_rate;
            uint32_t numerator = (uint32_t)(reverse ? r->out_rate - phase_remainder : phase_remainder);
            if (reverse) sample_base_idx++;
            faar_float2 position = ff2_mulf(ff2_div(ff2_uint32(numerator),
                    ff2_uint32(r->out_rate)), (float)r->num_phases);
            p_idx = (size_t)position.hi;
            if (p_idx >= r->num_phases / 2) p_idx = r->num_phases / 2 - 1;
            precise_delta = ff2_sub(position, ff2((float)p_idx));
            if (ff2_round(precise_delta) < 0.0f && p_idx) {
                --p_idx;
                precise_delta = ff2_add(precise_delta, ff2(1));
            }
            delta = ff2_round(precise_delta);
        }
#endif
#ifdef FAAR_WIDE_HIGH
        else if (r->polyphase_wide_table) {
            double position = (double)phase_remainder / r->out_rate;
            reverse = position > 0.5;
            if (reverse) {
                sample_base_idx++;
                position = 1 - position;
            }
            position *= (double)r->num_phases;
            p_idx = (size_t)position;
            if (p_idx >= r->num_phases / 2)
                p_idx = r->num_phases / 2 - 1;
            wide_delta = position - (double)p_idx;
            delta = (faar_sample_t)wide_delta;
        }
#endif
        else {
            frac = (faar_sample_t)phase_remainder / r->out_rate;
            /* Reflection changes the sign of fractional delay. Anchor negative
               phases at the next input sample so the evaluated position agrees. */
            reverse = frac > (faar_sample_t)0.5;
            if (reverse) {
                sample_base_idx++;
                frac = 1 - frac;
            }
            faar_sample_t phase_exact = frac * (faar_sample_t)r->num_phases;
            p_idx = (size_t)phase_exact;
            if (p_idx >= r->num_phases / 2)
                p_idx = r->num_phases / 2 - 1;
            delta = phase_exact - (faar_sample_t)p_idx;
        }
        if (sample_base_idx + filter_len > r->hist_len) {
            break;
        }

#ifndef FAAR_WIDE_HIGH
        if (r->polyphase_low_table) {
            if (r->exact_phase_bank) precise_delta = ff2(delta);
            filter_precise_phase(r, sample_base_idx, p_idx, precise_delta, reverse,
                                 out_samples + out_frames * r->channels);
            goto advance_output;
        }
#endif
#ifdef FAAR_WIDE_HIGH
        if (r->polyphase_wide_table) {
            if (r->exact_phase_bank) wide_delta = delta;
            filter_wide_phase(r, sample_base_idx, p_idx, wide_delta, reverse,
                              out_samples + out_frames * r->channels);
            goto advance_output;
        }
#endif
        if ((filter_len & 1) && p_idx == 0 && delta < exact_phase_eps) {
            const faar_sample_t * restrict coeffs = poly_table;
            const faar_sample_t center_coeff = coeffs[half_len];

            for (uint16_t c = 0; c < r->channels; c++) {
                const faar_sample_t * restrict h = &r->hist[c][sample_base_idx];
                faar_sample_t sum = (faar_sample_t)center_coeff * h[half_len];

                for (size_t j = 0; j < half_len; j++) {
                    sum += (faar_sample_t)coeffs[j] * ((faar_sample_t)h[j] + h[filter_len - 1 - j]);
                }

                out_samples[out_frames * r->channels + c] = sum;
            }
        } else {
            const faar_sample_t * restrict c1 = poly_table + p_idx * filter_len;
            const faar_sample_t * restrict c0 = c1 - filter_len;
            const faar_sample_t * restrict c2 = c1 + filter_len;
            const faar_sample_t * restrict c3 = c2 + filter_len;
            const faar_sample_t * restrict coeffs;

            /* Exact rational phases need no four-row interpolation. */
            if (delta < exact_phase_eps) {
                if (reverse && r->polyphase_reverse_table) {
                    coeffs = r->polyphase_reverse_table
                        + filter_len + p_idx * filter_len;
                } else if (reverse) {
                    for (size_t j = 0; j < filter_len; j++)
                        temp_coeffs[filter_len - 1 - j] = c1[j];
                    coeffs = temp_coeffs;
                } else {
                    coeffs = c1;
                }
            } else {
            /* Four-point Lagrange weights reproduce cubics exactly, allowing
             * a smaller phase bank without the leading cubic phase error. */
            faar_sample_t d = delta;
            faar_sample_t d2 = d * d;
            faar_sample_t d3 = d2 * d;
            faar_sample_t w0 = (-d3 + 3 * d2 - 2 * d) / 6;
            faar_sample_t w1 = (d3 - 2 * d2 - d + 2) / 2;
            faar_sample_t w2 = (-d3 + d2 + 2 * d) / 2;
            faar_sample_t w3 = (d3 - d) / 6;


            for (size_t j = 0; j < filter_len; j++) {
                size_t target = reverse ? filter_len - 1 - j : j;
                temp_coeffs[target] = (faar_sample_t)(w0 * c0[j] + w1 * c1[j] + w2 * c2[j] + w3 * c3[j]);
            }
                coeffs = temp_coeffs;
            }

            for (uint16_t c = 0; c < r->channels; c++) {
                const faar_sample_t * restrict h = &r->hist[c][sample_base_idx];
                faar_sample_t accumulator0 = 0, accumulator1 = 0;
                faar_sample_t accumulator2 = 0, accumulator3 = 0;
                size_t j = 0;
                for (; j + 3 < filter_len; j += 4) {
                    accumulator0 += (faar_sample_t)h[j] * coeffs[j];
                    accumulator1 += (faar_sample_t)h[j + 1] * coeffs[j + 1];
                    accumulator2 += (faar_sample_t)h[j + 2] * coeffs[j + 2];
                    accumulator3 += (faar_sample_t)h[j + 3] * coeffs[j + 3];
                }
                faar_sample_t accumulator = accumulator0 + accumulator1
                    + accumulator2 + accumulator3;
                for (; j < filter_len; ++j)
                    accumulator += (faar_sample_t)h[j] * coeffs[j];
                out_samples[out_frames * r->channels + c] = (float)accumulator;
            }
        }

advance_output:
        out_frames++;
        base_idx += (size_t)step_whole;
        phase_remainder += step_remainder;
        if (phase_remainder >= r->out_rate) {
            phase_remainder -= r->out_rate;
            ++base_idx;
        }
        if (r->exact_phase_bank) {
            phase_slot += phase_slot_step;
            if (phase_slot >= phase_slot_modulus)
                phase_slot -= phase_slot_modulus;
        }
    }

    r->output_frames += out_frames;
    size_t consumed = base_idx;
    r->phase = (uint64_t)base_idx * r->out_rate + phase_remainder;
    /* The next clock position may be beyond currently buffered input
     * (notably a 1-tap bank with an integral sampling step). Discard all
     * available history, preserving the pending skip in the phase clock. */
    size_t discard = consumed < r->hist_len ? consumed : r->hist_len;
    if (discard && (discard >= filter_len / 2
                || r->hist_len - discard < filter_len)) {
        for (uint16_t c = 0; c < r->channels; ++c)
            r->hist[c] += discard;
        r->hist_offset += discard;
        r->hist_len -= discard;
        r->phase -= (uint64_t)discard * r->out_rate;
    }

    apply_dither(r, out_samples, out_frames);
    return out_frames;
}

static size_t process_internal(faar_resampler * restrict r, const float * restrict in_samples, size_t in_count,
                           float * restrict out_samples, size_t max_out_count) {
    if (!r || !out_samples || (r->flushed && in_samples && in_count)) return 0;
    /* Flushing may drain a cascaded stage through a zero-frame process call. */
    if (r->flushed && !in_samples && !in_count) return 0;

    if (r->dft_prefilter) {
        if (in_count > (SIZE_MAX - 8192) / r->dft_expansion
                || in_count * r->dft_expansion + 8192
                    > SIZE_MAX / r->channels / sizeof(float)) {
            r->last_error = FAAR_ERR_INVALID_ARGUMENT;
            return 0;
        }
        size_t temp_capacity = in_count * r->dft_expansion + 8192;
        if (!r->stage2 && ensure_dft_intermediate(r, temp_capacity)) {
            r->last_error = FAAR_ERR_NO_MEMORY;
            return 0;
        }
        size_t filtered = 0;
        if (in_samples) r->input_frames += in_count;
        if (faar_dft_prefilter_process(r->dft_prefilter, in_samples, in_count,
                r->dft_intermediate, r->stage2 ? 0
                    : (max_out_count < temp_capacity ? max_out_count : temp_capacity),
                &filtered)) { r->last_error = FAAR_ERR_PROCESSING; return 0; }
        size_t produced;
        if (r->stage2) {
            produced = process_prefilter_output(r, temp_capacity,
                    out_samples, max_out_count);
        } else {
            produced = filtered;
            for (size_t i = 0; i < filtered; ++i)
                for (uint16_t c = 0; c < r->channels; ++c)
                    out_samples[i * r->channels + c] = r->dft_intermediate[c * filtered + i];
            apply_dither(r, out_samples, produced);
        }
        r->output_frames += produced;
        return produced;
    }

    if (r->stage1 && r->stage2) {
        size_t produced = 0, stage1_produced;
        size_t capacity = r->cascade_cap / r->channels;
        if (in_samples) r->input_frames += in_count;
        max_out_count = output_capacity(r, max_out_count);
        do {
            stage1_produced = process_internal(r->stage1, in_samples, in_count, r->cascade_buf, capacity);
            produced += process_internal(r->stage2, r->cascade_buf, stage1_produced,
                                     out_samples + produced * r->channels, max_out_count - produced);
            in_samples = NULL;
            in_count = 0;
        } while (stage1_produced == capacity);
        r->output_frames += produced;
        return produced;
    }

    return polyresample_process_single(r, in_samples, in_count, out_samples, max_out_count);
}

/* The prefilter FIFO is already planar. Drain directly into the downstream
 * append area, then advance the same history and boundary state as an input
 * copy would. Reserve only the actual burst, including during flush. */
static size_t process_prefilter_output(faar_resampler *parent, size_t capacity,
        float *out_samples, size_t max_out_count) {
    faar_resampler *r = parent->stage2;
    size_t in_count = faar_dft_prefilter_pending(parent->dft_prefilter);
    if (in_count > capacity) in_count = capacity;
    if (in_count) {
        if (reserve_history(r, in_count, false)) return 0;
        float *planes[MAX_CHANNELS];
        for (uint16_t c = 0; c < r->channels; ++c)
            planes[c] = r->hist[c] + r->hist_len;
        faar_dft_prefilter_drain_planes(parent->dft_prefilter, planes, in_count);
        if (!r->input_frames && !r->flushed) {
            for (uint16_t c = 0; c < r->channels; ++c) {
                faar_sample_t first = r->hist[c][r->history_pad];
                for (size_t j = 0; j < r->history_pad; ++j)
                    r->hist[c][j] = first;
            }
        }
        r->hist_len += in_count;
        if (!r->flushed) {
            for (uint16_t c = 0; c < r->channels; ++c)
                r->last_input[c] = r->hist[c][r->hist_len - 1];
            r->input_frames += in_count;
        }
    }
    return polyresample_process_single(r, NULL, 0, out_samples, max_out_count);
}

static size_t polyresample_flush_single(faar_resampler * r, float *out_samples, size_t max_out_count) {
    if (!r || !out_samples) return 0;

    if (r->in_rate == r->out_rate) {
        r->flushed = true;
        return polyresample_process_single(r, NULL, 0, out_samples, max_out_count);
    }
    if (!r->flushed) {
        size_t tail_len = (r->filter_len - 1) / 2 + 1;
        if (r->filter_len == 20) ++tail_len;
        if (reserve_history(r, tail_len, false)) return 0;
        for (uint16_t c = 0; c < r->channels; ++c) {
            faar_sample_t last = r->input_frames ? r->last_input[c] : 0.0f;
            for (size_t j = 0; j < tail_len; ++j)
                r->hist[c][r->hist_len + j] = last;
        }
        r->hist_len += tail_len;
        r->flushed = true;
    }

    return polyresample_process_single(r, NULL, 0, out_samples, max_out_count);
}

static size_t flush_internal(faar_resampler * r, float *out_samples, size_t max_out_count) {
    if (!r || !out_samples) return 0;

    if (r->dft_prefilter) {
        r->flushed = true;
        max_out_count = output_capacity(r, max_out_count);
        const size_t temp_capacity = 8192;
        if (!r->stage2 && ensure_dft_intermediate(r, temp_capacity)) {
            r->last_error = FAAR_ERR_NO_MEMORY;
            return 0;
        }
        size_t produced = 0, filtered;
        if (!r->stage2) {
            size_t capacity = max_out_count < temp_capacity ? max_out_count : temp_capacity;
            if (faar_dft_prefilter_flush(r->dft_prefilter, r->dft_intermediate,
                        capacity, &filtered)) { r->last_error = FAAR_ERR_PROCESSING; return 0; }
            for (size_t i = 0; i < filtered; ++i)
                for (uint16_t c = 0; c < r->channels; ++c)
                    out_samples[i * r->channels + c] = r->dft_intermediate[c * filtered + i];
            r->output_frames += filtered;
            apply_dither(r, out_samples, filtered);
            return filtered;
        }
        do {
            filtered = 0;
            if (faar_dft_prefilter_flush(r->dft_prefilter, NULL, 0, &filtered)) {
                r->last_error = FAAR_ERR_PROCESSING;
                return 0;
            }
            filtered = faar_dft_prefilter_pending(r->dft_prefilter);
            if (filtered > temp_capacity) filtered = temp_capacity;
            produced += process_prefilter_output(r, temp_capacity,
                    out_samples + produced * r->channels, max_out_count - produced);
            if (r->stage2->last_error != FAAR_OK) return 0;
        } while (filtered == temp_capacity);
        produced += flush_internal(r->stage2,
                out_samples + produced * r->channels,
                max_out_count - produced);
        r->output_frames += produced;
        return produced;
    }

    if (r->stage1 && r->stage2) {
        size_t produced = 0, stage1_flushed;
        size_t capacity = r->cascade_cap / r->channels;
        r->flushed = true;
        max_out_count = output_capacity(r, max_out_count);
        do {
            stage1_flushed = flush_internal(r->stage1, r->cascade_buf, capacity);
            if (stage1_flushed)
                produced += process_internal(r->stage2, r->cascade_buf,
                        stage1_flushed, out_samples + produced * r->channels,
                        max_out_count - produced);
        } while (stage1_flushed == capacity);
        produced += flush_internal(r->stage2, out_samples + produced * r->channels, max_out_count - produced);
        r->output_frames += produced;
        return produced;
    }

    return polyresample_flush_single(r, out_samples, max_out_count);
}

static void destroy_internal(faar_resampler * r) {
    if (!r) return;
    if (r->stage1) destroy_internal(r->stage1);
    if (r->stage2) destroy_internal(r->stage2);
    if (r->dft_prefilter) faar_dft_prefilter_destroy(r->dft_prefilter);
    FreeMemory(r->dft_intermediate);
    if (r->cascade_buf) aligned_free(r->cascade_buf);
    for (uint16_t c = 0; c < r->channels && c < MAX_CHANNELS; c++) {
        if (r->hist[c]) aligned_free(r->hist[c] - r->hist_offset);
    }
    if (!r->shared_coeffs) {
        if (r->polyphase_reverse_table) aligned_free(r->polyphase_reverse_table);
        if (r->polyphase_table) aligned_free(r->polyphase_table);
#ifdef FAAR_WIDE_HIGH
        aligned_free(r->polyphase_wide_table);
        aligned_free(r->polyphase_wide_reverse);
#endif
#ifndef FAAR_WIDE_HIGH
        if (r->polyphase_low_table) aligned_free(r->polyphase_low_table);
#endif
    }
    faar_design *plan = r->plan;
    FreeMemory(r);
    if (plan) plan_release(plan);
}

static void plan_release(faar_design *plan) {
#if !defined(__STDC_NO_ATOMICS__) && defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
    int last = atomic_fetch_sub_explicit(&plan->references, 1, memory_order_acq_rel) == 1;
#elif defined(_MSC_VER)
    int last = _InterlockedDecrement(&plan->references) == 0;
#else
    int last = --plan->references == 0;
#endif
    if (last) {
        destroy_internal(plan->prototype);
        FreeMemory(plan);
    }
}

/* Retain only immutable design state. Capacities and initial stream counters
 * remain as geometry for clones; every mutable allocation is released. */
static void release_plan_workspace(faar_resampler *r) {
    if (r->stage1) release_plan_workspace(r->stage1);
    if (r->stage2) release_plan_workspace(r->stage2);
    if (r->dft_prefilter) faar_dft_prefilter_release_workspace(r->dft_prefilter);
    FreeMemory(r->dft_intermediate);
    r->dft_intermediate = NULL;
    aligned_free(r->cascade_buf);
    r->cascade_buf = NULL;
    for (uint16_t c = 0; c < r->channels; ++c) {
        if (r->hist[c]) aligned_free(r->hist[c] - r->hist_offset);
        r->hist[c] = NULL;
    }
    r->hist_offset = 0;
}

static faar_resampler *clone_plan_internal(const faar_resampler *source) {
    faar_resampler *r = AllocMemory(sizeof(*r));
    if (!r) return NULL;
    *r = *source;
    r->shared_coeffs = true;
    r->plan = NULL;
    r->stage1 = r->stage2 = NULL;
    r->dft_prefilter = NULL;
    r->cascade_buf = NULL;
    r->dft_intermediate = NULL;
    r->hist_offset = 0;
    memset(r->hist, 0, sizeof(r->hist));
    if (source->stage1 && !(r->stage1 = clone_plan_internal(source->stage1))) goto fail;
    if (source->stage2 && !(r->stage2 = clone_plan_internal(source->stage2))) goto fail;
    if (source->dft_prefilter && !(r->dft_prefilter =
                faar_dft_prefilter_clone_filter(source->dft_prefilter))) goto fail;
    if (r->cascade_cap) {
        r->cascade_buf = aligned_malloc(r->cascade_cap * sizeof(float));
        if (!r->cascade_buf) goto fail;
    }
    if (r->hist_cap_per_ch) {
        for (uint16_t c = 0; c < r->channels; ++c) {
            r->hist[c] = aligned_malloc(r->hist_cap_per_ch * sizeof(*r->hist[c]));
            if (!r->hist[c]) goto fail;
            memset(r->hist[c], 0, r->hist_cap_per_ch * sizeof(*r->hist[c]));
        }
    }
    return r;
fail:
    destroy_internal(r);
    return NULL;
}

FAARAPI faar_status faar_design_open(const faar_config *config, faar_design **out) {
    if (!out) return FAAR_ERR_INVALID_ARGUMENT;
    *out = NULL;
    faar_resampler *prototype = NULL;
    faar_status status = open_stream(config, &prototype, false);
    if (status != FAAR_OK) return status;
    faar_design *plan = AllocMemory(sizeof(*plan));
    if (!plan) { destroy_internal(prototype); return FAAR_ERR_NO_MEMORY; }
#if !defined(__STDC_NO_ATOMICS__) && defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
    atomic_init(&plan->references, 1);
#else
    plan->references = 1;
#endif
    plan->prototype = prototype;
    plan->max_input_frames = config->max_input_frames;
    release_plan_workspace(prototype);
    *out = plan;
    return FAAR_OK;
}

FAARAPI faar_status faar_design_close(faar_design **design) {
    if (!design) return FAAR_ERR_INVALID_ARGUMENT;
    if (*design) plan_release(*design);
    *design = NULL;
    return FAAR_OK;
}

FAARAPI faar_status faar_resampler_open_from_design(faar_design *design,
                                                faar_resampler **out) {
    if (!out) return FAAR_ERR_INVALID_ARGUMENT;
    *out = NULL;
    if (!design) return FAAR_ERR_INVALID_ARGUMENT;
    if (plan_retain(design)) return FAAR_ERR_NO_MEMORY;
    faar_resampler *r = clone_plan_internal(design->prototype);
    if (!r) { plan_release(design); return FAAR_ERR_NO_MEMORY; }
    r->plan = design;
    if (design->max_input_frames) {
        faar_status st = reserve_stream(r, design->max_input_frames);
        if (st != FAAR_OK) { destroy_internal(r); return st; }
    }
    *out = r;
    return FAAR_OK;
}

FAARAPI faar_status faar_config_init(faar_config *config, uint32_t caller_size) {
    faar_config defaults;
    if (!config || caller_size < FAAR_CONFIG_BASELINE)
        return FAAR_ERR_INVALID_ARGUMENT;
    memset(&defaults, 0, sizeof(defaults));
    defaults.quality = FAAR_QUALITY_GOOD;
    uint32_t n = caller_size < sizeof(defaults) ? caller_size : sizeof(defaults);
    defaults.struct_size = n;
    memcpy(config, &defaults, n);
    return FAAR_OK;
}

FAARAPI faar_status faar_get_library_info(faar_library_info *out_info) {
    if (!out_info || out_info->struct_size < FAAR_LIBRARY_INFO_BASELINE)
        return FAAR_ERR_INVALID_ARGUMENT;
    uint32_t caller_size = out_info->struct_size;
    faar_library_info info = {0};
    info.max_channels = MAX_CHANNELS;
#ifndef FAAR_WIDE_HIGH
    info.flags |= FAAR_LIB_FLOAT_ONLY;
#endif
    info.version = FAAR_VERSION_STRING;
    info.copyright =
        "FAAR - Freeware Advanced Audio Resampler (https://freewareadvancedaudio.github.io)\n"
        " Copyright (C) 2026, Nils Schimmelmann";
    uint32_t n = caller_size < sizeof(info) ? caller_size : sizeof(info);
    info.struct_size = n;
    memcpy(out_info, &info, n);
    return FAAR_OK;
}

FAARAPI const char *faar_strerror(faar_status status) {
    switch (status) {
        case FAAR_OK: return "success";
        case FAAR_ERR_INVALID_ARGUMENT: return "invalid argument or state";
        case FAAR_ERR_UNSUPPORTED: return "unsupported configuration";
        case FAAR_ERR_NO_MEMORY: return "out of memory";
        case FAAR_ERR_PROCESSING: return "resampling failed; discard this stream";
        default: return "unknown FAAR status";
    }
}

static faar_status open_stream(const faar_config *config, faar_resampler **out,
                               bool reserve) {
    if (!out) return FAAR_ERR_INVALID_ARGUMENT;
    *out = NULL;
    if (!config || config->struct_size < FAAR_CONFIG_BASELINE)
        return FAAR_ERR_INVALID_ARGUMENT;
    faar_config cfg = {0};
    uint32_t n = config->struct_size < sizeof(cfg) ? config->struct_size : sizeof(cfg);
    memcpy(&cfg, config, n);
    uint32_t dither;
    uint8_t dither_depth;
    if (cfg.in_rate == 0 || cfg.out_rate == 0
            || cfg.channels == 0 || cfg.channels > MAX_CHANNELS
            || cfg.quality < FAAR_QUALITY_FAST || cfg.quality > FAAR_QUALITY_HIGH
            || dither_from_flags(cfg.flags, &dither, &dither_depth) != 0)
        return FAAR_ERR_INVALID_ARGUMENT;
    faar_resampler * r = create_resampler_internal(cfg.in_rate, cfg.out_rate,
            (uint16_t)cfg.channels, cfg.quality, cfg.decimate_factor, 0, false, 0);
    if (!r) return FAAR_ERR_NO_MEMORY;
    if (set_dither_internal(r, dither, dither_depth) != 0) {
        destroy_internal(r);
        return FAAR_ERR_INVALID_ARGUMENT;
    }
    if (reserve && cfg.max_input_frames) {
        faar_status st = reserve_stream(r, cfg.max_input_frames);
        if (st != FAAR_OK) { destroy_internal(r); return st; }
    }
    *out = r;
    return FAAR_OK;
}

FAARAPI faar_status faar_resampler_open(const faar_config *config,
                                         faar_resampler **out) {
    return open_stream(config, out, true);
}

static faar_status reserve_stream(faar_resampler *r, size_t max_input_frames) {
    if (max_input_frames > SIZE_MAX / r->channels / sizeof(float))
        return FAAR_ERR_INVALID_ARGUMENT;
    clear_error(r);
    size_t output_bound;
    faar_status st = prepare_internal(r, max_input_frames, &output_bound);
    if (st != FAAR_OK) return st;
    r->prepared_max_input = max_input_frames;
    set_prepared(r, true);
    return FAAR_OK;
}

FAARAPI faar_status faar_resampler_close(faar_resampler **resampler) {
    if (!resampler) return FAAR_ERR_INVALID_ARGUMENT;
    destroy_internal(*resampler);
    *resampler = NULL;
    return FAAR_OK;
}

FAARAPI faar_status faar_resampler_get_info(const faar_resampler *resampler,
                                             faar_resampler_info *out_info) {
    if (!resampler || !out_info || out_info->struct_size < FAAR_RESAMPLER_INFO_BASELINE)
        return FAAR_ERR_INVALID_ARGUMENT;
    uint32_t caller_size = out_info->struct_size;
    faar_resampler_info info = {0};
    info.decimate_factor = 1;
    for (const faar_resampler *s = resampler; s && s->stage1; s = s->stage2)
        info.decimate_factor *= (uint32_t)((s->in_rate + s->stage1->out_rate / 2)
                                           / s->stage1->out_rate);
    info.delay_frames = (uint32_t)resampler->delay;
    uint32_t n = caller_size < sizeof(info) ? caller_size : sizeof(info);
    info.struct_size = n;
    memcpy(out_info, &info, n);
    return FAAR_OK;
}

FAARAPI faar_status faar_resampler_process(faar_resampler *resampler,
        const float *in_samples, size_t in_count, float *out_samples,
        size_t max_out_count, size_t *out_count) {
    if (!out_count) return FAAR_ERR_INVALID_ARGUMENT;
    *out_count = 0;
    if (!resampler || !out_samples || (in_count && !in_samples)
            || (resampler->flushed && in_count)
            || (resampler->prepared && in_count > resampler->prepared_max_input)
            || in_count > SIZE_MAX / resampler->channels / sizeof(float)
            || max_out_count > SIZE_MAX / resampler->channels / sizeof(float))
        return FAAR_ERR_INVALID_ARGUMENT;
    clear_error(resampler);
    *out_count = process_internal(resampler, in_samples, in_count,
                              out_samples, max_out_count);
    faar_status status = find_error(resampler);
    if (status != FAAR_OK) *out_count = 0;
    return status;
}

FAARAPI faar_status faar_resampler_finalize(faar_resampler *resampler,
        float *out_samples, size_t max_out_count, size_t *out_count) {
    if (!out_count) return FAAR_ERR_INVALID_ARGUMENT;
    *out_count = 0;
    if (!resampler || !out_samples
            || max_out_count > SIZE_MAX / resampler->channels / sizeof(float))
        return FAAR_ERR_INVALID_ARGUMENT;
    clear_error(resampler);
    *out_count = flush_internal(resampler, out_samples, max_out_count);
    faar_status status = find_error(resampler);
    if (status != FAAR_OK) *out_count = 0;
    return status;
}
