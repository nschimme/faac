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
/*
 * Focused libfaar quality and streaming regressions:
 * - exact ceil-duration counts and flush behavior, including short/equal-rate streams;
 * - output invariance across input block sizes and output capacities;
 * - retained endpoint impulses and high-quality passband/alias energy;
 * - TPDF16 activity, quantization, and RNG sequence stability.
 * All failures use explicit checks so release builds remain effective with NDEBUG.
 */
#include <faar.h>
#include "faar_test_api.h"

#define TEST_QUALITY FAAR_QUALITY_HIGH
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846264338327950288
#define MAX_CAP 16384u
#define TEST_CHANNELS (MAX_CHANNELS < 2 ? MAX_CHANNELS : 2)

typedef struct samples {
    float *data;
    size_t frames;
    size_t capacity;
    uint16_t channels;
} samples;

static int fail(const char *what) {
    fprintf(stderr, "FAIL: %s\n", what);
    return 1;
}

static size_t expected_frames(size_t in_frames, uint32_t in_rate, uint32_t out_rate) {
    uint64_t n = (uint64_t)in_frames;
    return (size_t)((n / in_rate) * out_rate
        + ((n % in_rate) * out_rate + in_rate - 1) / in_rate);
}

static int append(samples *s, const float *src, size_t n) {
    if (n > SIZE_MAX - s->frames) return 0;
    size_t need = s->frames + n;
    if (need > s->capacity) {
        size_t cap = s->capacity ? s->capacity : 256;
        while (cap < need) {
            if (cap > SIZE_MAX / 2) return 0;
            cap *= 2;
        }
        float *p = (float *)realloc(s->data, cap * s->channels * sizeof(float));
        if (!p) return 0;
        s->data = p;
        s->capacity = cap;
    }
    if (n) memcpy(s->data + s->frames * s->channels, src,
                  n * s->channels * sizeof(float));
    s->frames += n;
    return 1;
}

/* Process every input frame, draining between calls and flushing until empty. */
static int convert_boundary(const float *input, size_t in_frames, uint32_t in_rate,
                   uint32_t out_rate, uint16_t channels, size_t block,
                   size_t out_cap, samples *out) {
    faar_test_handle * h = faar_test_create(in_rate, out_rate, channels, TEST_QUALITY);
    if (!h) return fail("faar_test_create returned NULL");
    out->data = NULL;
    out->frames = out->capacity = 0;
    out->channels = channels;
    float *buf = (float *)malloc(out_cap * channels * sizeof(float));
    if (!buf) { faar_test_close(h); return fail("output allocation failed"); }

    for (size_t pos = 0; pos < in_frames;) {
        size_t n = in_frames - pos < block ? in_frames - pos : block;
        size_t made = faar_test_process(h, input + pos * channels, n, buf, out_cap);
        if (!append(out, buf, made)) { free(buf); faar_test_close(h); return fail("output growth failed"); }
        pos += n;
        size_t drain_guard;
        for (drain_guard = 0; drain_guard < 100000; drain_guard++) {
            made = faar_test_process(h, NULL, 0, buf, out_cap);
            if (!made) break;
            if (!append(out, buf, made)) { free(buf); faar_test_close(h); return fail("drain growth failed"); }
        }
        if (drain_guard == 100000) { free(buf); faar_test_close(h); return fail("process drain did not terminate"); }
    }

    for (size_t guard = 0; guard < 100000; guard++) {
        size_t made = faar_test_flush(h, buf, out_cap);
        if (!made) break;
        if (!append(out, buf, made)) { free(buf); faar_test_close(h); return fail("flush growth failed"); }
        if (guard == 99999) { free(buf); faar_test_close(h); return fail("flush did not terminate"); }
    }
    /* A completed flush must remain empty on subsequent calls. */
    for (int i = 0; i < 2; i++) {
        if (faar_test_flush(h, buf, out_cap) != 0) {
            free(buf); faar_test_close(h); return fail("repeated flush emitted extra frames");
        }
    }
    free(buf);
    faar_test_close(h);
    return 0;
}

static int convert(const float *input, size_t in_frames, uint32_t in_rate,
                   uint32_t out_rate, uint16_t channels, size_t block,
                   size_t out_cap, samples *out) {
    return convert_boundary(input,in_frames,in_rate,out_rate,channels,block,out_cap,out);
}

static int test_boundary_modes(void) {
    const uint32_t rates[][2]={{96000,44100},{48000,44100},{44100,48000},
        {192000,44100},{48000,96000},{44100,88200},{44100,96000},
        {48000,88200},{96000,192000},{192000,96000},{192000,48000},
        {32000,44100},{32000,48000},{88200,96000},
        {48000,48000}};
    const size_t lengths[]={1,17,9600};
    for(size_t r=0;r<sizeof(rates)/sizeof(rates[0]);r++) {
        for(size_t nidx=0;nidx<sizeof(lengths)/sizeof(lengths[0]);nidx++) {
            size_t n=lengths[nidx];
            float *input=malloc(n*TEST_CHANNELS*sizeof(*input));
            if(!input)return fail("boundary input allocation failed");
            for(size_t j=0;j<n;j++)
                for(unsigned c=0;c<TEST_CHANNELS;c++)
                    input[j*TEST_CHANNELS+c]=c==0?.5f:-.25f;
            samples output;
            int rc=convert(input,n,rates[r][0],rates[r][1],TEST_CHANNELS,17,17,&output);
            free(input);
            if(rc)return rc;
            if(output.frames!=expected_frames(n,rates[r][0],rates[r][1])) {
                fprintf(stderr, "boundary count %u->%u input=%zu actual=%zu expected=%zu\n",
                        rates[r][0], rates[r][1], n, output.frames,
                        expected_frames(n,rates[r][0],rates[r][1]));
                free(output.data);return fail("boundary duration mismatch");
            }
            for(size_t j=0;j<output.frames;j++) {
                for(unsigned c=0;c<TEST_CHANNELS;c++) {
                    if(fabsf(output.data[j*TEST_CHANNELS+c]-(c==0?.5f:-.25f))>2e-6f) {
                        free(output.data);return fail("endpoint hold changed a constant signal");
                    }
                }
            }
            free(output.data);
        }
    }
    puts(TEST_CHANNELS == 2
         ? "endpoint hold: mono/stereo, short streams and cascades passed"
         : "endpoint hold: mono, short streams and cascades passed");
    return 0;
}

static float signal_value(size_t i, uint16_t c, size_t n) {
    if (i == 0) return c == 0 ? 1.0f : 0.0f;
    if (i + 1 == n) return c == 0 ? 0.75f : -0.5f;
    return (float)(0.25 * sin(i * 0.017 + c * 0.31) + 0.1 * cos(i * 0.071 + c * 0.19));
}

static int test_lengths(void) {
    static const size_t lengths[] = {1, 2, 17, 127, 255, 40000};
    static const uint32_t rates[][2] = {{48000,44100}, {192000,44100}, {48000,96000}, {192000,48000}, {48000,32000}, {88200,44100}, {32000,44100}, {32000,48000}, {88200,96000}, {48000,48000}};
    for (size_t r = 0; r < sizeof(rates)/sizeof(rates[0]); r++) {
        uint32_t ir = rates[r][0], orate = rates[r][1];
        for (size_t k = 0; k < sizeof(lengths)/sizeof(lengths[0]); k++) {
            size_t n = lengths[k];
            float *in = (float *)malloc(n * sizeof(float));
            if (!in) return fail("length test input allocation failed");
            for (size_t i = 0; i < n; i++) in[i] = signal_value(i, 0, n);
            samples out;
            int rc = convert(in, n, ir, orate, 1, 17, 257, &out);
            free(in);
            if (rc) return rc;
            size_t want = expected_frames(n, ir, orate);
            if (out.frames != want) {
                fprintf(stderr, "length %u->%u n=%zu: got %zu, expected %zu\n", ir, orate, n, out.frames, want);
                free(out.data); return 1;
            }
            free(out.data);
        }
    }
    return 0;
}

static int test_streaming_range(size_t first_rate, size_t last_rate) {
    const size_t n = 40000;
    static const uint32_t rates[][2] = {{48000,44100}, {192000,44100}, {48000,96000}, {192000,48000}, {48000,32000}, {88200,44100}, {32000,44100}, {32000,48000}, {88200,96000}, {48000,48000}, {47999,44100}};
    const size_t blocks[] = {1, 17, 257, 4096};
    const size_t caps[] = {1, 17, 257, 16384};
    if (last_rate > sizeof(rates)/sizeof(rates[0]) || first_rate >= last_rate)
        return 1;
    for (size_t r = first_rate; r < last_rate; r++) {
        uint32_t ir = rates[r][0], orate = rates[r][1];
        float *in = (float *)malloc(n * TEST_CHANNELS * sizeof(float));
        if (!in) return fail("stream test input allocation failed");
        for (size_t i = 0; i < n; i++) {
            for (uint16_t c = 0; c < TEST_CHANNELS; ++c)
                in[TEST_CHANNELS*i+c] = signal_value(i, c, n);
        }
        samples ref;
        if (convert(in, n, ir, orate, TEST_CHANNELS, n, 16384, &ref)) { free(in); return 1; }
        size_t want = expected_frames(n, ir, orate);
        if (ref.frames != want) { free(in); free(ref.data); return fail("reference length differs from ceil duration"); }

        for (size_t b = 0; b < sizeof(blocks)/sizeof(blocks[0]); b++) {
            for (size_t c = 0; c < sizeof(caps)/sizeof(caps[0]); c++) {
                samples got;
                if (convert(in, n, ir, orate, TEST_CHANNELS, blocks[b], caps[c], &got)) {
                    free(in); free(ref.data); return 1;
                }
                if (got.frames != want) { free(in); free(ref.data); free(got.data); return fail("stream variant frame count differs"); }
                float maxdiff = 0.0f;
                for (size_t i = 0; i < want * TEST_CHANNELS; i++) {
                    float d = fabsf(ref.data[i] - got.data[i]);
                    if (!isfinite(d)) { free(in); free(ref.data); free(got.data); return fail("non-finite streaming output difference"); }
                    if (d > maxdiff) maxdiff = d;
                }
                if (maxdiff > 1e-5f) {
                    fprintf(stderr, "stream %u->%u block=%zu cap=%zu maxdiff=%g\n", ir, orate, blocks[b], caps[c], maxdiff);
                    free(in); free(ref.data); free(got.data); return 1;
                }
                free(got.data);
            }
        }
        free(in); free(ref.data);
    }
    return 0;
}

static int test_streaming_0(void) { return test_streaming_range(0, 5); }
static int test_streaming_1(void) { return test_streaming_range(5, 11); }

static int test_impulses(void) {
    static const uint32_t rates[][2] = {{48000,44100}, {96000,44100}, {192000,44100}, {48000,96000}, {192000,48000}, {32000,44100}, {32000,48000}, {88200,96000}, {48000,48000}};
    const size_t n = 40000;
    for (size_t r = 0; r < sizeof(rates)/sizeof(rates[0]); r++) {
        for (int endpoint = 0; endpoint < 2; endpoint++) {
            float *in = (float *)calloc(n, sizeof(float));
            if (!in) return fail("impulse allocation failed");
            in[endpoint ? n-1 : 0] = 1.0f;
            samples out;
            int rc = convert(in, n, rates[r][0], rates[r][1], 1, 4096, 16384, &out);
            free(in);
            if (rc) return rc;
            size_t want = expected_frames(n, rates[r][0], rates[r][1]);
            if (out.frames != want) {
                fprintf(stderr, "impulse count %u->%u endpoint=%d actual=%zu expected=%zu\n",
                        rates[r][0], rates[r][1], endpoint, out.frames, want);
                free(out.data); return fail("impulse stream frame count mismatch");
            }
            size_t lo = endpoint ? (out.frames > 4 ? out.frames - 4 : 0) : 0;
            size_t hi = endpoint ? out.frames : (out.frames < 4 ? out.frames : 4);
            float energy = 0.0f;
            for (size_t i = lo; i < hi; i++) energy += fabsf(out.data[i]);
            if (!(energy > 1e-4f)) {
                fprintf(stderr, "%s impulse lost at %u->%u (edge energy=%g)\n", endpoint ? "end" : "start", rates[r][0], rates[r][1], energy);
                free(out.data); return 1;
            }
            free(out.data);
        }
    }
    return 0;
}

static int test_staged_short_impulse_symmetry(void) {
    static const uint32_t rates[][2] = {
        {96000,44100}, {48000,44100}, {192000,44100}, {44100,48000},
        {48000,96000}, {192000,48000}, {48000,32000}, {88200,44100},
        {32000,44100}, {32000,48000}, {88200,96000}
    };
    for (size_t r = 0; r < sizeof(rates) / sizeof(rates[0]); ++r) {
        uint32_t ir = rates[r][0], orate = rates[r][1];
        size_t in_frames = (size_t)ir * 2;
        float *input = (float *)calloc(in_frames, sizeof(*input));
        if (!input) return fail("symmetric impulse input allocation failed");
        input[ir] = 0.5f;
        samples output;
        int rc = convert(input, in_frames, ir, orate, 1, 4096, 16384, &output);
        free(input);
        if (rc) return rc;
        size_t center = expected_frames(ir, ir, orate);
        size_t radius = orate / 40;
        if (output.frames != expected_frames(in_frames, ir, orate)
                || center < radius || center + radius >= output.frames) {
            free(output.data);
            return fail("symmetric impulse window is outside output");
        }
        long double error = 0.0L, energy = 0.0L;
        for (size_t i = 1; i <= radius; ++i) {
            long double left = output.data[center - i];
            long double right = output.data[center + i];
            long double delta = left - right;
            error += delta * delta;
            energy += left * left + right * right;
        }
        free(output.data);
        long double relative = sqrtl(error / fmaxl(energy, 1e-300L));
        if (relative > 2e-5L) {
            fprintf(stderr, "staged-short impulse symmetry %u->%u relative error %.9Lg\n",
                    ir, orate, relative);
            return fail("staged-short centered impulse response is asymmetric");
        }
    }
    puts("staged-short centered High impulse responses are symmetric");
    return 0;
}

/* Least-squares fit y ~= a*sin(wt)+b*cos(wt), giving phase-independent gain and residual. */
static int test_passband(void) {
static const uint32_t rates[][2] = {{48000,44100}, {96000,44100}, {44100,48000}, {88200,44100},
                                        {16000,48000}, {11025,16000}};
    const double freqs[] = {1000.0, 5180.0, 7520.0, 10000.0, 14400.0, 15200.0, 20000.0};
    for (size_t r = 0; r < sizeof(rates)/sizeof(rates[0]); r++) {
        uint32_t ir = rates[r][0], orate = rates[r][1];
        size_t n = ir; /* One second at each input rate. */
        for (size_t f = 0; f < sizeof(freqs)/sizeof(freqs[0]); f++) {
            if (freqs[f] >= 0.475 * (ir < orate ? ir : orate)) continue;
            float *in = (float *)malloc(n * sizeof(float));
            if (!in) return fail("tone allocation failed");
            for (size_t i = 0; i < n; i++) in[i] = (float)sin(2.0 * PI * freqs[f] * i / ir);
            samples out;
            int rc = convert(in, n, ir, orate, 1, n, 16384, &out);
            free(in);
            if (rc) return rc;
            /* Long DFT plans need a wide settling window. Endpoint behavior is
             * covered independently. */
            size_t guard = orate / 10;
            if (guard < 1024) guard = 1024;
            size_t begin = guard, end = out.frames > guard
                ? out.frames - guard : out.frames;
            double ss=0, cc=0, sc=0, sy=0, cy=0;
            size_t m=0;
            for (size_t i = begin; i < end; i++) {
                double wt = 2.0 * PI * freqs[f] * i / orate;
                double s = sin(wt), c = cos(wt), y = out.data[i];
                if (!isfinite(y)) { free(out.data); return fail("non-finite tone output"); }
                ss += s*s; cc += c*c; sc += s*c; sy += s*y; cy += c*y; m++;
            }
            double det = ss*cc - sc*sc;
            if (!m || !isfinite(det) || det <= 0.0) { free(out.data); return fail("invalid tone-fit matrix"); }
            double a = (sy*cc - cy*sc)/det;
            double b = (cy*ss - sy*sc)/det;
            double gain = hypot(a,b);
            if (!isfinite(a) || !isfinite(b) || !isfinite(gain)) { free(out.data); return fail("non-finite tone fit"); }
            double sse = 0.0;
            for (size_t i = begin; i < end; i++) {
                double wt = 2.0 * PI * freqs[f] * i / orate;
                double predicted = a * sin(wt) + b * cos(wt);
                double residual = (double)out.data[i] - predicted;
                if (!isfinite(residual)) { free(out.data); return fail("non-finite tone residual"); }
                sse += residual * residual;
            }
            double rms = sqrt(sse / m);
            printf("tone %u->%u %.0f Hz: gain %.8f residual_rms %.3g\n", ir, orate, freqs[f], gain, rms);
            free(out.data);
            /* The contract is a 0.25 dB response tolerance. Mixed High
               retains the calibrated float32 projection ceiling; standalone
               float needs the higher FFT and accumulation noise ceiling. */
            double gain_db = 20.0 * log10(gain);
#ifdef FAAR_WIDE_HIGH
            double residual_limit = sqrt(0.5) * pow(10.0, -144.684142 / 20.0);
#else
            double residual_limit = 4e-7;
#endif
            if (!isfinite(rms) || !isfinite(gain_db)
                    || fabs(gain_db) > 0.25 || rms > residual_limit) {
                fprintf(stderr, "passband failure at %u->%u %.0f Hz: gain=%g (%g dB) residual=%g limit=%g\n",
                        ir, orate, freqs[f], gain, gain_db, rms, residual_limit);
                return 1;
            }
        }
    }
    return 0;
}

static int test_48k_32k_wideband(void) {
    const uint32_t ir = 48000, orate = 32000;
    const double freqs[] = {14400.0, 15200.0};
    const size_t n = ir;
    for (size_t f = 0; f < sizeof(freqs) / sizeof(freqs[0]); ++f) {
        float *in = (float *)malloc(n * sizeof(*in));
        if (!in) return fail("48->32 passband input allocation failed");
        for (size_t i = 0; i < n; ++i)
            in[i] = (float)sin(2.0 * PI * freqs[f] * i / ir);
        samples out;
        int rc = convert(in, n, ir, orate, 1, n, 16384, &out);
        free(in);
        if (rc) return rc;
        size_t begin = orate / 10, end = out.frames - begin;
        double ss=0.0, cc=0.0, sc=0.0, sy=0.0, cy=0.0;
        for (size_t i = begin; i < end; ++i) {
            double wt = 2.0 * PI * freqs[f] * i / orate;
            double s = sin(wt), c = cos(wt), y = out.data[i];
            ss += s*s; cc += c*c; sc += s*c; sy += s*y; cy += c*y;
        }
        double det = ss*cc - sc*sc;
        double a = (sy*cc - cy*sc) / det;
        double b = (cy*ss - sy*sc) / det;
        double gain_db = 20.0 * log10(hypot(a,b));
        free(out.data);
        printf("48->32 High passband %.0f Hz: %.5f dB\n", freqs[f], gain_db);
        if (!isfinite(gain_db) || fabs(gain_db) > 0.25)
            return fail("48->32 High passband response exceeded 0.25 dB");
    }
    return 0;
}

/* Check passband level near the top of the input audio band for the common
   2x upsample plan. These tones stay below 0.49 * input rate. */
static int test_48k_96k_wideband(void) {
    const uint32_t ir = 48000, orate = 96000;
    const double freqs[] = {1000.0, 21600.0, 22800.0, 23520.0};
    const size_t n = ir;
    float *input = (float *)malloc(n * sizeof(*input));
    if (!input) return fail("48->96 wideband tone allocation failed");
    for (size_t f = 0; f < sizeof(freqs) / sizeof(freqs[0]); ++f) {
        for (size_t i = 0; i < n; ++i)
            input[i] = (float)sin(2.0 * PI * freqs[f] * i / ir);
        samples output;
        int rc = convert(input, n, ir, orate, 1, n, 16384, &output);
        if (rc) { free(input); return rc; }
        size_t guard = orate / 10, begin = guard, end = output.frames - guard;
        double ss=0.0, cc=0.0, sc=0.0, sy=0.0, cy=0.0;
        for (size_t i = begin; i < end; ++i) {
            double wt = 2.0 * PI * freqs[f] * i / orate;
            double s = sin(wt), c = cos(wt), y = output.data[i];
            ss += s*s; cc += c*c; sc += s*c; sy += s*y; cy += c*y;
        }
        double det = ss*cc - sc*sc;
        if (!isfinite(det) || det <= 0.0) {
            free(output.data); free(input);
            return fail("48->96 wideband tone-fit matrix invalid");
        }
        double a = (sy*cc - cy*sc)/det;
        double b = (cy*ss - sy*sc)/det;
        double gain_db = 20.0 * log10(hypot(a, b));
        printf("tone %u->%u %.0f Hz: gain %.8f dB\n", ir, orate, freqs[f], gain_db);
        free(output.data);
        if (!isfinite(gain_db) || fabs(gain_db) > 0.25) {
            fprintf(stderr, "wideband gain failure at %.0f Hz: %.6f dB\n",
                    freqs[f], gain_db);
            free(input);
            return 1;
        }
    }
    free(input);
    return 0;
}

static int run_dither(uint32_t ir, uint32_t orate, size_t block, size_t cap,
                      int enabled, samples *out) {
    const size_t n = 1024;
    faar_test_handle * h = faar_test_create(ir, orate, 1, TEST_QUALITY);
    if (!h) return fail("dither create failed");
    if (enabled && faar_test_set_dither(h, FAAR_DITHER_TPDF, 16) != 0) {
        faar_test_close(h); return fail("set TPDF16 failed");
    }
    out->data = NULL; out->frames = out->capacity = 0; out->channels = 1;
    float *buf = (float *)malloc(cap * sizeof(float));
    float *zeros = (float *)calloc(n, sizeof(float));
    if (!buf || !zeros) { free(buf); free(zeros); faar_test_close(h); return fail("dither buffers allocation failed"); }
    for (size_t pos = 0; pos < n;) {
        size_t take = n - pos < block ? n - pos : block;
        size_t made = faar_test_process(h, zeros + pos, take, buf, cap);
        if (!append(out, buf, made)) { free(buf); free(zeros); faar_test_close(h); return fail("dither output allocation failed"); }
        pos += take;
        size_t drain_guard;
        for (drain_guard = 0; drain_guard < 10000; drain_guard++) {
            made = faar_test_process(h, NULL, 0, buf, cap);
            if (!made) break;
            if (!append(out, buf, made)) { free(buf); free(zeros); faar_test_close(h); return fail("dither drain allocation failed"); }
        }
        if (drain_guard == 10000) { free(buf); free(zeros); faar_test_close(h); return fail("dither process drain did not terminate"); }
    }
    for (size_t guard = 0; guard < 10000; guard++) {
        size_t made = faar_test_flush(h, buf, cap);
        if (!made) break;
        if (!append(out, buf, made)) { free(buf); free(zeros); faar_test_close(h); return fail("dither flush allocation failed"); }
        if (guard == 9999) { free(buf); free(zeros); faar_test_close(h); return fail("dither flush did not terminate"); }
    }
    free(buf); free(zeros); faar_test_close(h);
    return 0;
}

static int test_dither_streams(void) {
    static const uint32_t rates[][2] = {{96000,48000}, {48000,96000}, {32000,44100}, {32000,48000}, {88200,96000}, {48000,48000}};
    for (size_t r = 0; r < sizeof(rates)/sizeof(rates[0]); r++) {
        samples plain, small, large;
        if (run_dither(rates[r][0], rates[r][1], 17, 17, 0, &plain)
            || run_dither(rates[r][0], rates[r][1], 1, 1, 1, &small)
            || run_dither(rates[r][0], rates[r][1], 257, 257, 1, &large)) return 1;
        if (plain.frames != small.frames || small.frames != large.frames) {
            free(plain.data); free(small.data); free(large.data); return fail("dither stream lengths differ");
        }
        size_t nonzero = 0;
        for (size_t i = 0; i < plain.frames; i++) {
            if (plain.data[i] != 0.0f) { free(plain.data); free(small.data); free(large.data); return fail("dither-off zero input was nonzero"); }
            if (small.data[i] != large.data[i]) { free(plain.data); free(small.data); free(large.data); return fail("dither RNG sequence changed with block/capacity"); }
            if (small.data[i] != 0.0f) {
                nonzero++;
                if (fabsf(small.data[i] - nearbyintf(small.data[i])) > 1e-6f) {
                    free(plain.data); free(small.data); free(large.data); return fail("TPDF16 output is not integer-quantized");
                }
            }
        }
        if (!nonzero) { free(plain.data); free(small.data); free(large.data); return fail("TPDF16 did not dither zero input"); }
        printf("TPDF16 zero input %u->%u: %zu nonzero samples; sequence stable across chunking\n",
               rates[r][0], rates[r][1], nonzero);
        free(plain.data); free(small.data); free(large.data);
    }
    return 0;
}

/* Measure total output energy above Nyquist; do not project onto a presumed alias tone. */
static int test_alias_energy(void) {
    static const struct {
        uint32_t in_rate, out_rate;
        size_t frames;
        double first_frequency, second_frequency;
    } rates[] = {
        {96000, 44100, 96000, 22060.0, 23000.0},
        {192000, 44100, 192000, 22060.0, 23000.0},
        {96000, 48000, 96000, 24010.0, 0.0},
        {192000, 48000, 192000, 24100.0, 94080.0},
        {48000, 32000, 48000, 16100.0, 16950.0},
        {88200, 44100, 88200, 22150.0, 23000.0},
    };
    for (size_t r = 0; r < sizeof(rates)/sizeof(rates[0]); ++r) {
      const uint32_t ir = rates[r].in_rate, orate = rates[r].out_rate;
      const size_t n = rates[r].frames;
      const double freqs[] = {rates[r].first_frequency, rates[r].second_frequency};
      for (size_t f = 0; f < sizeof(freqs)/sizeof(freqs[0]); f++) {
        if (freqs[f] == 0.0) continue;
        float *in = (float *)malloc(n * sizeof(float));
        if (!in) return fail("alias input allocation failed");
        double input_energy = 0.0;
        for (size_t i = 0; i < n; i++) {
            double x = sin(2.0 * PI * freqs[f] * i / ir);
            in[i] = (float)x; /* Full-scale peak amplitude 1.0. */
            input_energy += x * x;
        }
        samples out;
        int rc = convert(in, n, ir, orate, 1, n, 16384, &out);
        free(in);
        if (rc) return rc;
        /* A long linear-phase prefilter needs its full settling interval
           excluded from this steady-state alias measurement. Endpoint impulse
           behavior is covered separately by the boundary group. */
        size_t guard = orate / 10;
        if (guard < 1024) guard = 1024;
        size_t begin = guard;
        size_t end = out.frames > guard ? out.frames - guard : out.frames;
        double output_energy = 0.0;
        for (size_t i = begin; i < end; i++) {
            double y = out.data[i];
            if (!isfinite(y)) { free(out.data); return fail("non-finite alias output sample"); }
            output_energy += y * y;
        }
        size_t input_count = n, output_count = end - begin;
        if (!output_count || !isfinite(input_energy) || input_energy <= 0.0
            || !isfinite(output_energy) || output_energy < 0.0) {
            free(out.data); return fail("invalid alias energy");
        }
        double input_rms = sqrt(input_energy / input_count);
        double output_rms = sqrt(output_energy / output_count);
        double attenuation_db = output_rms == 0.0 ? INFINITY : 20.0 * log10(input_rms / output_rms);
        printf("alias %u->%u %.0f Hz: output RMS %.3g, attenuation %.2f dB\n",
               ir, orate, freqs[f], output_rms, attenuation_db);
        free(out.data);
        if (!isfinite(attenuation_db) && output_rms != 0.0) return fail("invalid alias attenuation");
        if (attenuation_db < 135.0) {
            fprintf(stderr, "alias attenuation failure at %.0f Hz: %.2f dB\n", freqs[f], attenuation_db);
            return 1;
        }
      }
    }
    return 0;
}

/* The 150 dB High probe, including a loud neighbouring
 * channel to expose leakage from the two-real-channels-in-one-complex FFT. */
static int test_high_stopband(void) {
    const uint32_t ir = 96000, orate = 44100;
    const size_t n = 3 * (size_t)ir;
    const double peak = 0.6309573444801932; /* -4 dBFS */
    static const uint16_t channel_counts[] = {1, 2, 3, 8};
    faar_library_info library = {.struct_size = sizeof(library)};
    if (faar_get_library_info(&library) != FAAR_OK) return fail("library query failed");
    for (size_t k = 0; k < sizeof(channel_counts)/sizeof(channel_counts[0]); ++k) {
        uint16_t channels = channel_counts[k];
        if (channels > library.max_channels) continue;
        float *in = malloc(n * channels * sizeof(*in));
        if (!in) return fail("High stopband input allocation failed");
        for (size_t i = 0; i < n; ++i) {
            for (uint16_t c = 0; c < channels; ++c) {
                double frequency = c & 1 ? 1000.0 : 23000.0;
                in[i * channels + c] = (float)(32768.0 * peak
                    * sin(2.0 * PI * frequency * i / ir));
            }
        }
        samples out;
        int rc = convert(in, n, ir, orate, channels, 4096, 8192, &out);
        free(in);
        if (rc) return rc;
        if (out.frames != 3 * (size_t)orate) {
            free(out.data); return fail("High stopband output frame count");
        }
        size_t guard = orate / 2, end = out.frames - guard;
        for (uint16_t c = 0; c < channels; c += 2) {
            double energy = 0;
            for (size_t i = guard; i < end; ++i) {
                double x = out.data[i * channels + c] / 32768.0;
                energy += x * x;
            }
            double rms = sqrt(energy / (end - guard));
            double rejection = rms == 0 ? INFINITY : 20 * log10(peak / sqrt(2.0) / rms);
            printf("High stopband channels=%u channel=%u rejection=%.3f dB\n",
                   channels, c, rejection);
            if (!isfinite(energy) || rejection < 150.0) {
                free(out.data); return fail("High stopband rejection below 150 dB");
            }
        }
        free(out.data);
    }
    return 0;
}

static int test_high_up_residual(void) {
    static const uint32_t rates[][2] = {{44100, 96000}, {48000, 88200}, {88200, 96000}};
    const double peak = 0.6309573444801932;
    for (size_t r = 0; r < sizeof(rates) / sizeof(rates[0]); ++r) {
        const uint32_t ir = rates[r][0], orate = rates[r][1];
        const size_t n = 2 * (size_t)ir;
        const double frequency = 0.48 * ir;
        float *in = malloc(n * sizeof(*in));
        if (!in) return fail("High up residual input allocation failed");
        for (size_t i = 0; i < n; ++i)
            in[i] = (float)(32768.0 * peak * sin(2.0 * PI * frequency * i / ir));
        samples out;
        int rc = convert(in, n, ir, orate, 1, 4096, 8192, &out);
        free(in);
        if (rc) return rc;
        size_t guard = orate / 4, end = out.frames - guard;
        double ss = 0, cc = 0, sc = 0, sy = 0, cy = 0, yy = 0;
        for (size_t i = guard; i < end; ++i) {
            double s = sin(2.0 * PI * frequency * i / orate);
            double c = cos(2.0 * PI * frequency * i / orate);
            double y = out.data[i] / 32768.0;
            ss += s * s; cc += c * c; sc += s * c;
            sy += s * y; cy += c * y; yy += y * y;
        }
        free(out.data);
        double det = ss * cc - sc * sc;
        double a = (sy * cc - cy * sc) / det, b = (cy * ss - sy * sc) / det;
        double fit = a * a * ss + 2 * a * b * sc + b * b * cc;
        double residual = (yy - fit) / (end - guard);
        double db = 10 * log10((residual > 1e-30 ? residual : 1e-30)
                               / (peak * peak / 2.0));
        printf("High up residual %u->%u %.1f dB\n", ir, orate, db);
        if (!isfinite(db) || db > -110.0)
            return fail("High up residual above -110 dB");
    }
    return 0;
}

static int test_empty_flush(void) {
    faar_test_handle * h = faar_test_create(48000, 44100, 1, TEST_QUALITY);
    float out[8];
    if (!h) return fail("empty flush create failed");
    if (faar_test_flush(h, out, 8) || faar_test_flush(h, out, 8)) {
        faar_test_close(h); return fail("empty stream flush emitted frames");
    }
    faar_test_close(h);
    return 0;
}

#include "test_groups.h"
int main(int argc, char **argv) {
    const faar_test_group groups[] = {
        {"boundary", test_boundary_modes}, {"lengths", test_lengths},
        {"streaming-0", test_streaming_0},
        {"streaming-1", test_streaming_1}, {"impulses", test_impulses},
        {"staged_short_symmetry", test_staged_short_impulse_symmetry},
        {"passband", test_passband},
        {"48k_96k_wideband", test_48k_96k_wideband},
        {"48k_32k_wideband", test_48k_32k_wideband},
        {"alias", test_alias_energy},
        {"high_stopband", test_high_stopband},
        {"high_up_residual", test_high_up_residual},
        {"empty", test_empty_flush}, {"dither", test_dither_streams}
    };
    return faar_run_groups(argc, argv, groups, sizeof(groups) / sizeof(groups[0]));
}
