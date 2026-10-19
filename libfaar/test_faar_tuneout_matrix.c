/*
 * FAAR adaptation of Tuneout Resampler's portable matrix/streaming checks.
 *
 * Copyright 2026 Studio Nodoka
 * Modified for libfaar by Nils Schimmelmann.
 * Licensed under Apache-2.0; see verification/LICENSE and verification/NOTICE.
 *
 * This is local verification tooling. It is not linked into libfaar.
 */
#include <faar.h>
#include <ctype.h>
#include <math.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHANNEL_LIMIT 8
#define INPUT_FRAMES 257
#define OUTPUT_CHUNK 19
#define FINGERPRINT_SCALE 65536.0
#define FNV_OFFSET UINT64_C(14695981039346656037)
#define FNV_PRIME UINT64_C(1099511628211)

typedef struct audio audio;
struct audio {
    float *samples;
    size_t frames;
    size_t capacity;
    uint16_t channels;
};
static int fail(const char *message, uint32_t in_rate, uint32_t out_rate,
                unsigned channels);

static uint64_t matrix_fingerprints[3] = {FNV_OFFSET, FNV_OFFSET, FNV_OFFSET};
static int matrix_completed;

static void hash_u32(uint64_t *hash, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) {
        *hash ^= (uint8_t)(value >> (i * 8));
        *hash *= FNV_PRIME;
    }
}

static void hash_output(uint64_t *hash, uint32_t in_rate, uint32_t out_rate,
                        faar_quality_t quality, const audio *output) {
    hash_u32(hash, in_rate);
    hash_u32(hash, out_rate);
    hash_u32(hash, (uint32_t)quality);
    hash_u32(hash, (uint32_t)output->frames);
    for (size_t i = 0; i < output->frames; ++i) {
        int32_t fixed = (int32_t)llround((double)output->samples[i] * FINGERPRINT_SCALE);
        hash_u32(hash, (uint32_t)fixed);
    }
}

static int check_fingerprints(void) {
    static const char *path = FAAR_FINGERPRINT_FIXTURE_PATH;
    if (getenv("FAAR_RECORD_FINGERPRINTS")) {
        for (unsigned q = 0; q < 3; ++q)
            printf("%u %016" PRIx64 "\n", q, matrix_fingerprints[q]);
        return 0;
    }
    FILE *f = fopen(path, "r");
    if (!f) return fail("could not open portable fingerprint fixture", 0, 0, 1);
    for (unsigned q = 0; q < 3; ++q) {
        unsigned index;
        uint64_t expected;
        if (fscanf(f, "%u %" SCNx64, &index, &expected) != 2
            || index != q || expected != matrix_fingerprints[q]) {
            fclose(f);
            return fail("portable regression fingerprint changed", 0, 0, 1);
        }
    }
    int trailing;
    do { trailing = fgetc(f); } while (trailing != EOF && isspace((unsigned char)trailing));
    fclose(f);
    return trailing == EOF ? 0 : fail("fingerprint fixture has extra records", 0, 0, 1);
}

static int fail(const char *message, uint32_t in_rate, uint32_t out_rate,
                unsigned channels) {
    fprintf(stderr, "FAIL: %s (%u -> %u Hz, %u channels)\n", message,
            in_rate, out_rate, channels);
    return 1;
}

static size_t expected_frames(size_t input, uint32_t in_rate, uint32_t out_rate) {
    uint64_t n = input;
    return (size_t)((n / in_rate) * out_rate
        + ((n % in_rate) * out_rate + in_rate - 1) / in_rate);
}

static int append(audio *dst, const float *src, size_t frames) {
    if (frames > SIZE_MAX - dst->frames) return 0;
    size_t needed = dst->frames + frames;
    if (needed > dst->capacity) {
        size_t cap = dst->capacity ? dst->capacity : 64;
        while (cap < needed) {
            if (cap > SIZE_MAX / 2) return 0;
            cap *= 2;
        }
        if (cap > SIZE_MAX / dst->channels / sizeof(float)) return 0;
        float *p = (float *)realloc(dst->samples, cap * dst->channels * sizeof(float));
        if (!p) return 0;
        dst->samples = p;
        dst->capacity = cap;
    }
    if (frames) memcpy(dst->samples + dst->frames * dst->channels, src,
                       frames * dst->channels * sizeof(float));
    dst->frames += frames;
    return 1;
}

static int drain(faar_handle_t h, audio *dst, float *buffer, size_t cap,
                 uint32_t in_rate, uint32_t out_rate) {
    for (size_t guard = 0; guard < 100000; ++guard) {
        size_t made = faar_process(h, NULL, 0, buffer, cap);
        if (!made) return 0;
        if (!append(dst, buffer, made)) return fail("output allocation failed", in_rate, out_rate, dst->channels);
    }
    return fail("process drain did not terminate", in_rate, out_rate, dst->channels);
}

static int convert(const float *input, size_t frames, uint32_t in_rate,
                   uint32_t out_rate, uint16_t channels, size_t input_block,
                   size_t output_cap, faar_quality_t quality, audio *result) {
    faar_handle_t h = faar_create(in_rate, out_rate, channels, quality);
    if (!h) return fail("faar_create failed", in_rate, out_rate, channels);
    result->samples = NULL;
    result->frames = result->capacity = 0;
    result->channels = channels;
    float *buffer = (float *)malloc(output_cap * channels * sizeof(float));
    if (!buffer) {
        faar_destroy(h);
        return fail("temporary output allocation failed", in_rate, out_rate, channels);
    }
    for (size_t pos = 0; pos < frames;) {
        size_t count = frames - pos < input_block ? frames - pos : input_block;
        size_t made = faar_process(h, input + pos * channels, count, buffer, output_cap);
        if (!append(result, buffer, made)) {
            free(buffer); faar_destroy(h);
            return fail("output allocation failed", in_rate, out_rate, channels);
        }
        pos += count;
        if (drain(h, result, buffer, output_cap, in_rate, out_rate)) {
            free(buffer); faar_destroy(h); return 1;
        }
    }
    for (size_t guard = 0; guard < 100000; ++guard) {
        size_t made = faar_flush(h, buffer, output_cap);
        if (!made) break;
        if (!append(result, buffer, made)) {
            free(buffer); faar_destroy(h);
            return fail("flush output allocation failed", in_rate, out_rate, channels);
        }
        if (guard == 99999) {
            free(buffer); faar_destroy(h);
            return fail("flush did not terminate", in_rate, out_rate, channels);
        }
    }
    free(buffer);
    faar_destroy(h);
    return 0;
}

static float sample(size_t frame, unsigned channel) {
    uint64_t x = ((uint64_t)frame * CHANNEL_LIMIT + channel)
        * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
    int64_t signed_top = (int64_t)(uint32_t)(x >> 32) - INT64_C(2147483648);
    return (float)((double)signed_top / 4294967296.0);
}

static size_t load_rates(uint32_t *rates, size_t capacity) {
    FILE *f = fopen(FAAR_RATE_FIXTURE_PATH, "r");
    if (!f) return 0;
    size_t count = 0;
    unsigned value;
    while (count < capacity && fscanf(f, "%u", &value) == 1) rates[count++] = value;
    fclose(f);
    return count;
}

static size_t build_pairs(const uint32_t *rates, size_t count,
                          uint32_t (*pairs)[2], size_t capacity) {
    size_t n = 0;
    for (size_t i = 0; i < count; ++i)
        for (size_t j = 0; j < count; ++j) {
            if (n == capacity) return 0;
            pairs[n][0] = rates[i]; pairs[n++][1] = rates[j];
        }
    for (size_t i = 0; i < count; ++i)
        for (int delta = -1; delta <= 1; delta += 2) {
            uint32_t neighbor = rates[i] + delta;
            if (n + 2 > capacity) return 0;
            pairs[n][0] = rates[i]; pairs[n++][1] = neighbor;
            pairs[n][0] = neighbor; pairs[n++][1] = rates[i];
        }
    return n;
}

static int test_matrix_streaming(void) {
    uint32_t rates[32];
    uint32_t pairs[400][2];
    size_t rate_count = load_rates(rates, 32);
    size_t pair_count = build_pairs(rates, rate_count, pairs, 400);
    if (rate_count != 17 || pair_count != 357) return fail("rate fixture or pair count is wrong", 0, 0, 1);

    float input[INPUT_FRAMES];
    for (size_t i = 0; i < INPUT_FRAMES; ++i) input[i] = sample(i, 0);
    static const faar_quality_t qualities[] = {
        FAAR_QUALITY_FAST, FAAR_QUALITY_GOOD, FAAR_QUALITY_HIGH
    };
    for (size_t p = 0; p < pair_count; ++p) {
        uint32_t in_rate = pairs[p][0], out_rate = pairs[p][1];
        for (size_t q = 0; q < sizeof(qualities) / sizeof(qualities[0]); ++q) {
            audio reference, streamed;
            size_t cap = expected_frames(INPUT_FRAMES, in_rate, out_rate) + 2;
            if (convert(input, INPUT_FRAMES, in_rate, out_rate, 1, INPUT_FRAMES,
                        cap, qualities[q], &reference)) return 1;
            if (reference.frames != expected_frames(INPUT_FRAMES, in_rate, out_rate)) {
                fprintf(stderr, "expected=%zu actual=%zu quality=%d\n",
                        expected_frames(INPUT_FRAMES, in_rate, out_rate),
                        reference.frames, (int)qualities[q]);
                free(reference.samples);
                return fail("matrix output frame count mismatch", in_rate, out_rate, 1);
            }
            if (convert(input, INPUT_FRAMES, in_rate, out_rate, 1, 17,
                        OUTPUT_CHUNK, qualities[q], &streamed)) {
                free(reference.samples); return 1;
            }
            if (streamed.frames != reference.frames) {
                free(reference.samples); free(streamed.samples);
                return fail("chunked frame count mismatch", in_rate, out_rate, 1);
            }
            for (size_t i = 0; i < reference.frames; ++i) {
                if (!isfinite(reference.samples[i]) || !isfinite(streamed.samples[i])
                    || fabsf(reference.samples[i] - streamed.samples[i]) > 2e-6f) {
                    free(reference.samples); free(streamed.samples);
                    return fail("chunking changed output samples", in_rate, out_rate, 1);
                }
            }
            hash_output(&matrix_fingerprints[q], in_rate, out_rate,
                        qualities[q], &reference);
            free(reference.samples); free(streamed.samples);
        }
    }
    printf("Tuneout-derived FAAR rate matrix: %zu ordered pairs x 3 presets passed\n", pair_count);
    matrix_completed = 1;
    return 0;
}

static int test_recorded_fingerprints(void) {
    if (!matrix_completed && test_matrix_streaming()) return 1;
    return check_fingerprints();
}

static long double oracle_i0(long double x) {
    long double term = 1.0L, sum = 1.0L;
    for (unsigned k = 1; k < 256; ++k) {
        long double factor = x / (2.0L * k);
        term *= factor * factor;
        sum += term;
        if (term < sum * 1e-24L) break;
    }
    return sum;
}

static long double oracle_high_coefficient(size_t length, size_t tap,
        long double cutoff, long double beta, long double scale) {
    const long double pi = acosl(-1.0L);
    long double values[5493], total = 0.0L;
    for (size_t j = 0; j < length; ++j) {
        long double n = (long double)j - (long double)(length - 1) * 0.5L;
        long double sinc = fabsl(n) < 1e-20L ? 2.0L * cutoff
            : sinl(2.0L * pi * cutoff * n) / (pi * n);
        long double position = 2.0L * j / (length - 1) - 1.0L;
        long double window = oracle_i0(beta * sqrtl(1.0L - position * position))
            / oracle_i0(beta);
        values[j] = sinc * window;
        total += values[j];
    }
    return scale * values[tap] / total;
}


static int test_high_precision_coefficients(void) {
    const size_t frames = 2048, impulse_frame = 512;
    float *input = (float *)calloc(frames, sizeof(float));
    if (!input) return fail("coefficient oracle input allocation failed", 44100, 88200, 1);
    input[impulse_frame] = 1.0f;
    audio output;
    int rc = convert(input, frames, 44100, 88200, 1, frames, frames * 2,
                     FAAR_QUALITY_HIGH, &output);
    free(input);
    if (rc) return 1;
    /* 44.1 -> 88.2 kHz is a 2x zero-inserted DFT plan: the independent
       long-double oracle checks its 5,493-tap Kaiser prefilter at the
       centered sample and the adjacent half-rate interpolation sample. */
    long double expected_phase0 = oracle_high_coefficient(5493, 2746,
        0.24889387661163931L, 19.078143572239966L, 2.0L);
    long double expected_half = oracle_high_coefficient(5493, 2747,
        0.24889387661163931L, 19.078143572239966L, 2.0L);
    double actual_phase0 = output.samples[2 * impulse_frame];
    double actual_half = output.samples[2 * impulse_frame + 1];
    free(output.samples);
    if (fabsl(actual_phase0 - expected_phase0) > 2e-7L
        || fabsl(actual_half - expected_half) > 2e-7L) {
        fprintf(stderr, "coefficient oracle mismatch: phase0 %.10g/%.10Lg, half %.10g/%.10Lg\n",
                actual_phase0, expected_phase0, actual_half, expected_half);
        return fail("independent high-precision coefficient check failed", 44100, 88200, 1);
    }
    puts("Independent high-precision FAAR coefficient probes passed");
    return 0;
}

static int isolation_for(faar_quality_t quality, float tolerance) {
    static const uint32_t rates[][2] = {
        {48000, 44100}, {96000, 44100}, {192000, 44100},
        {32000, 44100}, {32000, 48000}, {88200, 96000}, {8000, 768000}
    };
    static const uint16_t channel_counts[] = {2, 6, 8};
    for (size_t ri = 0; ri < sizeof(rates) / sizeof(rates[0]); ++ri) {
        uint32_t in_rate = rates[ri][0], out_rate = rates[ri][1];
        for (size_t ci = 0; ci < sizeof(channel_counts) / sizeof(channel_counts[0]); ++ci) {
            uint16_t channels = channel_counts[ci];
            float *interleaved = (float *)malloc(INPUT_FRAMES * channels * sizeof(float));
            if (!interleaved) return fail("channel test allocation failed", in_rate, out_rate, channels);
            for (size_t i = 0; i < INPUT_FRAMES; ++i)
                for (unsigned c = 0; c < channels; ++c)
                    interleaved[i * channels + c] = sample(i, c);
            audio combined;
            if (convert(interleaved, INPUT_FRAMES, in_rate, out_rate, channels,
                        17, OUTPUT_CHUNK, quality, &combined)) {
                free(interleaved); return 1;
            }
            for (uint16_t c = 0; c < channels; ++c) {
                float mono_input[INPUT_FRAMES];
                for (size_t i = 0; i < INPUT_FRAMES; ++i)
                    mono_input[i] = interleaved[i * channels + c];
                audio mono;
                if (convert(mono_input, INPUT_FRAMES, in_rate, out_rate, 1,
                            17, OUTPUT_CHUNK, quality, &mono)) {
                    free(combined.samples); free(interleaved); return 1;
                }
                if (mono.frames != combined.frames) {
                    free(mono.samples); free(combined.samples); free(interleaved);
                    return fail("channel output length differs from mono", in_rate, out_rate, channels);
                }
                for (size_t i = 0; i < mono.frames; ++i)
                    if (fabsf(mono.samples[i] - combined.samples[i * channels + c]) > tolerance) {
                        free(mono.samples); free(combined.samples); free(interleaved);
                        return fail("channel output depends on other channels", in_rate, out_rate, channels);
                    }
                free(mono.samples);
            }
            free(combined.samples);
            free(interleaved);
        }
    }
    return 0;
}

/* Paired-channel roundoff follows the build precision. */
static int test_channel_isolation(void) {
#ifdef FAAR_FLOAT
    const float tolerance = 4e-7f;
#else
    const float tolerance = 1e-12f;
#endif
    for (int q = 0; q < 3; ++q)
        if (isolation_for((faar_quality_t)q, tolerance)) return 1;
    puts("Tuneout-derived FAAR channel isolation: 2, 6 and 8 channels passed");
    return 0;
}

static int test_equal_rate_bit_identity(void) {
    float input[CHANNEL_LIMIT * 17];
    float output[CHANNEL_LIMIT * 17];
    for (size_t i = 0; i < 17 * CHANNEL_LIMIT; ++i) input[i] = sample(i, (unsigned)(i % CHANNEL_LIMIT));
    input[0] = 0.0f;
    input[1] = -0.0f;
    faar_handle_t h = faar_create(48000, 48000, CHANNEL_LIMIT, FAAR_QUALITY_HIGH);
    if (!h) return fail("equal-rate create failed", 48000, 48000, CHANNEL_LIMIT);
    size_t made = faar_process(h, input, 17, output, 17);
    size_t tail = faar_flush(h, output + made * CHANNEL_LIMIT, 17 - made);
    int ok = made + tail == 17
        && memcmp(input, output, sizeof(input)) == 0;
    faar_destroy(h);
    if (!ok) return fail("equal-rate bypass changed sample bits", 48000, 48000, CHANNEL_LIMIT);
    puts("Tuneout-derived FAAR equal-rate bypass: bit identity passed");
    return 0;
}

#include "verification/test_groups.h"
int main(int argc, char **argv) {
    const faar_test_group groups[] = {
        {"matrix", test_matrix_streaming}, {"fingerprints", test_recorded_fingerprints},
        {"coefficients", test_high_precision_coefficients},
        {"channels", test_channel_isolation}, {"equal-rate", test_equal_rate_bit_identity}
    };
    return faar_run_groups(argc, argv, groups, sizeof(groups) / sizeof(groups[0]));
}
