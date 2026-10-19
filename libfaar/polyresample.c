/*
 * libfaar - Polyphase Farrow-Interpolated Kaiser-Sinc Audio Resampler
 */

#include "faar.h"
#include "libfaar_internal.h"
#ifdef FAAR_STAGED_BACKEND
#include "dft_coeffs.h"
#include "dft_prefilter.h"
#endif
#include <stdalign.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef MAX_CHANNELS
#define MAX_CHANNELS 8
#endif

#define POLYRESAMPLE_NUM_PHASES 64 /* Fine fractional phase resolution for pre-calculated polyphase bank */
#define POLYRESAMPLE_MAX_FILTER_LEN 512 /* Upper bound for stack-allocated temporary coefficient buffers */
#define POLYRESAMPLE_ALIGNMENT 64 /* 64-byte alignment for AVX-512 / AVX2 / NEON SIMD vector loads */

#ifdef FAAR_DOUBLE_COEFFICIENTS
typedef double polyresample_coef_t;
#else
typedef float polyresample_coef_t;
#endif

/* Helper function for 64-byte SIMD memory allocation */
static void *aligned_malloc(size_t size) {
#if defined(_WIN32)
    return _aligned_malloc(size, POLYRESAMPLE_ALIGNMENT);
#elif defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200112L
    void *ptr = NULL;
    if (posix_memalign(&ptr, POLYRESAMPLE_ALIGNMENT, size) != 0) return NULL;
    return ptr;
#else
    void *ptr = malloc(size + POLYRESAMPLE_ALIGNMENT + sizeof(void *));
    if (!ptr) return NULL;
    uintptr_t addr = (uintptr_t)ptr + sizeof(void *) + POLYRESAMPLE_ALIGNMENT - 1;
    void *aligned_ptr = (void *)(addr & ~(uintptr_t)(POLYRESAMPLE_ALIGNMENT - 1));
    ((void **)aligned_ptr)[-1] = ptr;
    return aligned_ptr;
#endif
}

static void aligned_free(void *ptr) {
    if (!ptr) return;
#if defined(_WIN32)
    _aligned_free(ptr);
#elif defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200112L
    free(ptr);
#else
    free(((void **)ptr)[-1]);
#endif
}

/* Robust double-precision I0 Bessel function with dynamic convergence */
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

/* Calculate double-precision Kaiser window coefficient */
static double kaiser_window(double n, double N, double beta) {
    if (beta <= 0.0) return 1.0;
    double arg = 2.0 * n / N - 1.0;
    double sq = 1.0 - arg * arg;
    if (sq <= 0.0) return 0.0;
    return bessel_i0(beta * sqrt(sq)) / bessel_i0(beta);
}

static uint32_t rate_gcd(uint32_t a, uint32_t b) {
    while (b) { uint32_t remainder = a % b; a = b; b = remainder; }
    return a;
}

struct faar_resampler_struct {
    uint32_t in_rate;
    uint32_t out_rate;
    uint16_t channels;
    faar_quality_t quality;
    faar_dither_t dither;
    faar_boundary_t boundary;
    uint8_t dither_bits;
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
    polyresample_coef_t *polyphase_table; /* Half-bank and interpolation guards, shared by all channels */
    polyresample_coef_t *polyphase_reverse_table; /* Reversed exact rows, avoids per-output coefficient copies */

    polyresample_coef_t *hist[MAX_CHANNELS]; /* Keeps sample and coefficient arithmetic at the selected width */
    size_t hist_len;            /* Unconsumed frames in hist */
    size_t hist_cap_per_ch;     /* Allocated capacity per channel in frames */

    size_t delay;
    size_t history_pad;
    bool flushed;               /* Tracks if flush zero-padding has already been appended */

    /* Multi-stage cascade pointers with clean ownership model */
    struct faar_resampler_struct *stage1;
    struct faar_resampler_struct *stage2;
    float *cascade_buf;
    size_t cascade_cap;
#ifdef FAAR_STAGED_BACKEND
    faar_dft_prefilter *dft_prefilter;
    size_t dft_expansion;
    float *dft_intermediate;
    size_t dft_intermediate_capacity;
#endif
};

static size_t polyresample_process_single(faar_handle_t r, const float * restrict in_samples, size_t in_count,
                                           float * restrict out_samples, size_t max_out_count);

static size_t polyresample_flush_single(faar_handle_t r, float *out_samples, size_t max_out_count);

#ifdef FAAR_STAGED_BACKEND
static size_t polyresample_process_planar(faar_handle_t r,
        const float * restrict in_samples, size_t in_count,
        float * restrict out_samples, size_t max_out_count);

static int ensure_dft_intermediate(faar_handle_t r, size_t frames) {
    if (frames <= r->dft_intermediate_capacity) return 0;
    if (frames > SIZE_MAX / r->channels / sizeof(float)) return -1;
    float *next = (float *)realloc(r->dft_intermediate,
            frames * r->channels * sizeof(float));
    if (!next) return -1;
    r->dft_intermediate = next;
    r->dft_intermediate_capacity = frames;
    return 0;
}
#endif

static faar_handle_t faar_create_ext_internal(uint32_t in_rate,
        uint32_t out_rate, uint16_t channels, faar_quality_t quality,
        uint32_t decimate_factor, size_t filter_len_override,
        bool force_polyphase);

/* Limit padding to the original input duration, without multiplying the
   entire recording length by a rate. */
static size_t output_capacity(faar_handle_t r, size_t capacity) {
    if (r->flushed) {
        uint64_t target = (r->input_frames / r->in_rate) * r->out_rate
            + ((r->input_frames % r->in_rate) * r->out_rate + r->in_rate - 1) / r->in_rate;
        uint64_t remaining = target > r->output_frames ? target - r->output_frames : 0;
        if (remaining < capacity) capacity = (size_t)remaining;
    }
    return capacity;
}

static void apply_dither(faar_handle_t r, float *samples, size_t frames) {
    if (r->dither == FAAR_DITHER_OFF || !r->dither_bits) return;
    uint16_t c = 0;
    for (size_t i = 0; i < frames * r->channels; i++) {
        float sum = samples[i];
        float lsb_scale = (r->dither_bits == 16) ? 1.0f : (1.0f / 256.0f);
        float inv_lsb_scale = 1.0f / lsb_scale;

        /* 32-bit XorShift PRNG for fast uniform random numbers [0.0, 1.0) */
        uint32_t s1 = r->prng_state[c];
        s1 ^= s1 << 13; s1 ^= s1 >> 17; s1 ^= s1 << 5;
        r->prng_state[c] = s1;

        uint32_t s2 = s1 + 0x9e3779b9U;
        s2 ^= s2 << 13; s2 ^= s2 >> 17; s2 ^= s2 << 5;

        float r1 = (float)s1 * (1.0f / 4294967296.0f);
        float r2 = (float)s2 * (1.0f / 4294967296.0f);
        float tpdf = (r1 - r2) * lsb_scale; /* Triangular distribution in LSB units */

        float sample_scaled = sum;
        if (r->dither == FAAR_DITHER_SHAPED) {
            sample_scaled -= r->error_history[c] * 0.75f;
        }

        float quantized = (float)floor((double)((sample_scaled + tpdf) * inv_lsb_scale + 0.5f)) * lsb_scale;
        r->error_history[c] = quantized - sample_scaled;
        sum = quantized;
        samples[i] = sum;
        if (++c == r->channels) c = 0;
    }
}

FAARAPI faar_handle_t faar_create(uint32_t in_rate, uint32_t out_rate, uint16_t channels, faar_quality_t quality) {
    return faar_create_ext(in_rate, out_rate, channels, quality, 0);
}

#ifdef FAAR_STAGED_BACKEND
#ifdef FAAR_STAGED_SHORT_POLY
static bool short_half_rate_route(uint32_t in_rate, uint32_t out_rate) {
    return (uint64_t)in_rate == 2 * (uint64_t)out_rate
        && (in_rate == 44100 || in_rate == 48000 || in_rate == 88200
            || in_rate == 96000 || in_rate == 176400 || in_rate == 192000);
}

/* Band edges are fractions of the lower Nyquist frequency. */
static const struct {
    double passband, stopband, attenuation;
} preset_specs[] = {
    /* Fast preserves 90% bandwidth with 100 dB alias rejection. */
    [FAAR_QUALITY_FAST] = {0.90, 1.04, 100.0},
    /* Good preserves 96% bandwidth with 130 dB alias rejection. */
    [FAAR_QUALITY_GOOD] = {0.96, 1.04, 130.0},
    [FAAR_QUALITY_HIGH] = {0.96, 1.04, 150.0},
};

static void design_band(faar_quality_t quality, double pb, double sb,
        size_t alignment, double reserve_db, size_t *tap_count, double *beta, double *cutoff) {
    size_t preset = quality <= FAAR_QUALITY_HIGH ? (size_t)quality
                                                 : FAAR_QUALITY_GOOD;
    /* Shared full-screen Kaiser margin. Cascades reserve another 4.7 dB
     * for the extra float boundary and shorter final-filter sidelobes.
     * Edges are cycles per input sample. */
    double attenuation = preset_specs[preset].attenuation + 5.3 + reserve_db;
    size_t n = (size_t)ceil((attenuation - 7.95) / (14.36 * (sb - pb))) + 1;
    *tap_count = ((n - 1 + alignment - 1) / alignment) * alignment + 1;
    *beta = 0.1102 * (attenuation - 8.7);
    *cutoff = 0.5 * (pb + sb);
}

/* Kaiser length and beta for a preset spec at filter_rate. alignment is the
 * divisor N-1 must meet so the DFT stage stays centred on input zero. */
static void design_from_spec(faar_quality_t quality, uint32_t out_rate,
        uint32_t filter_rate, double stopband, size_t alignment, double reserve_db,
        size_t *tap_count, double *beta, double *cutoff) {
    size_t preset = quality <= FAAR_QUALITY_HIGH ? (size_t)quality
                                                 : FAAR_QUALITY_GOOD;
    double pb = preset_specs[preset].passband;
    double sb = stopband > 0.0 ? stopband : preset_specs[preset].stopband;
    design_band(quality, 0.5 * pb * out_rate / filter_rate,
                0.5 * sb * out_rate / filter_rate, alignment, reserve_db,
                tap_count, beta, cutoff);
}

struct short_plan {
    size_t taps, fft, expansion, decimation, bank;
    double beta, cutoff;
};

enum short_route { GENERAL, REFERENCE, UP, HALF, QUARTER, DOWN32 };

static const struct short_route_entry {
    uint32_t in, out;
    enum short_route route;
    size_t expansion, bank, high_bank;
} short_routes[] = {
    {48000, 44100, REFERENCE, 2, 20, 0},
    {96000, 44100, REFERENCE, 1, 20, 0},
    {192000, 44100, REFERENCE, 1, 20, 0},
    {192000, 48000, QUARTER, 1, 1, 0},
    {44100, 32000, DOWN32, 2, 21, 0},
    {48000, 32000, DOWN32, 2, 1, 0},
    {44100, 48000, UP, 2, 21, 0},
    {44100, 88200, UP, 2, 1, 0},
    {48000, 96000, UP, 2, 1, 0},
    {96000, 192000, UP, 2, 1, 0},
    {44100, 96000, UP, 2, 41, 21},
    {48000, 88200, UP, 2, 41, 21},
    /* Wider support suppresses phase modulation near input Nyquist. */
    {32000, 44100, UP, 2, 41, 0},
    {32000, 48000, UP, 2, 41, 0},
    {88200, 96000, UP, 2, 41, 21},
};

static const struct short_route_entry *short_dedicated_route(uint32_t in,
        uint32_t out) {
    for (size_t i = 0; i < sizeof(short_routes) / sizeof(*short_routes); ++i) {
        if (in == short_routes[i].in && out == short_routes[i].out)
            return &short_routes[i];
    }
    return NULL;
}

static struct short_plan short_plan(uint32_t in, uint32_t out,
        faar_quality_t quality, bool general) {
    struct short_plan p = {0, 1, 1, 1, 21, 19.078143572239966, 0.0};
    enum short_route route = short_half_rate_route(in, out) ? HALF : GENERAL;
    const struct short_route_entry *entry = short_dedicated_route(in, out);
    if (general) { route = GENERAL; entry = NULL; }
    if (entry) {
        route = entry->route;
        p.expansion = entry->expansion;
        p.bank = quality == FAAR_QUALITY_HIGH && entry->high_bank
            ? entry->high_bank : entry->bank;
    }
    switch (route) {
    case HALF:
        p.decimation = 2;
        p.bank = 1;
        p.taps = 5493;
        p.cutoff = 0.24889387661163931;
        break;
    case QUARTER:
        p.decimation = 4;
        p.taps = 8193;
        p.cutoff = 0.124411;
        break;
    case REFERENCE:
        p.taps = in == 192000 ? 3071 : 5981;
        p.cutoff = in == 192000 ? 0.1085 : 0.22867124913694359;
        if (in == 192000 && quality == FAAR_QUALITY_HIGH) p.fft = 32768;
        break;
    case UP:
        p.taps = in == 32000 && out == 48000 ? 4097 : 5493;
        if (in == 48000 || in == 96000) {
            p.taps = 5297;
            p.beta = 18.401802232573989;
        }
        p.cutoff = 0.24889387661163931;
        break;
    case DOWN32:
        break;
    case GENERAL:
        p.expansion = 2;
        /* Bank input must oversample the output by K: High's 0.96 Nyquist
         * residual brackets K in [2.48, 2.52], so take the centre. */
        const double bank_margin = 2.50;
        for (size_t d = 4; d; d /= 2) {
            for (size_t e = 1; e <= 2; ++e) {
                if ((uint64_t)in * e % d == 0
                        && (uint64_t)in * e >= bank_margin * out * d) {
                    p.decimation = d;
                    p.expansion = e;
                    goto selected;
                }
            }
        }
selected:
        /* Near-unity ratios cannot reach K with 2x expansion; the even
         * prototype preserves their wider bank bandwidth. */
        if ((double)in * p.expansion / p.decimation / out < bank_margin)
            p.bank = 20;
        if ((uint64_t)in * p.expansion % ((uint64_t)out * p.decimation) == 0)
            p.bank = 1;
        break;
    }
    if (route == GENERAL || route == DOWN32
            || quality != FAAR_QUALITY_HIGH
            || (route == REFERENCE && in == 192000)
            || (route == UP && in == 32000)) {
        /* This stop edge also covers the existing near-Nyquist alias probe. */
        uint32_t lower = in < out ? in : out;
        double sb = route == DOWN32 && in == 48000
            && quality == FAAR_QUALITY_HIGH ? 1.00625
            : route == REFERENCE && in == 192000
                && quality == FAAR_QUALITY_HIGH ? 1.00045 : 0.0;
        size_t alignment = p.expansion == 2 ? 4 : p.decimation;
        if (alignment < 2) alignment = 2;
        design_from_spec(quality, lower, in * (uint32_t)p.expansion, sb,
                         alignment, general ? 4.7 : 0.0, &p.taps, &p.beta, &p.cutoff);
    }
    return p;
}

#endif
static faar_handle_t faar_create_staged(uint32_t in_rate, uint32_t out_rate,
        uint16_t channels, faar_quality_t quality
#ifdef FAAR_STAGED_SHORT_POLY
        , const struct short_plan *selected
#endif
        ) {
    size_t tap_count, fft_size = 1;
    size_t expansion = 1;
    double beta;
    switch (quality) {
        case FAAR_QUALITY_FAST: tap_count = 151; beta = 8.0; break;
        case FAAR_QUALITY_HIGH: tap_count = 601; beta = 18.0; break;
        case FAAR_QUALITY_GOOD:
        default: tap_count = 301; beta = 12.0; break;
    }
#ifdef FAAR_STAGED_SHORT_POLY
    struct short_plan plan = *selected;
    tap_count = plan.taps;
    fft_size = plan.fft ? plan.fft : 1;
    expansion = plan.expansion;
    beta = plan.beta;
#endif
    while (fft_size < tap_count * 5) {
        if (fft_size > SIZE_MAX / 2) return NULL;
        fft_size <<= 1;
    }
    double *taps = (double *)malloc(tap_count * sizeof(*taps));
    if (!taps) return NULL;
#ifdef FAAR_STAGED_SHORT_POLY
    double cutoff = plan.cutoff;
#else
    double cutoff = 0.4794 * (double)out_rate / (double)in_rate;
#endif
    if (faar_dft_design_lowpass(taps, tap_count, cutoff, beta)) {
        free(taps);
        return NULL;
    }
    if (expansion > 1) {
        for (size_t i = 0; i < tap_count; ++i)
            taps[i] *= (double)expansion;
    }
    struct faar_resampler_struct *r =
        (struct faar_resampler_struct *)calloc(1, sizeof(*r));
    if (!r) { free(taps); return NULL; }
    r->in_rate = in_rate;
    r->out_rate = out_rate;
    r->channels = channels;
    r->quality = quality;
    r->ratio = (double)out_rate / (double)in_rate;
    r->boundary = FAAR_BOUNDARY_HOLD;
    r->dft_expansion = expansion;
#ifdef FAAR_STAGED_SHORT_POLY
    size_t decimation = plan.decimation;
#else
    size_t decimation = 1;
#endif
    r->dft_prefilter = faar_dft_prefilter_create_rate(channels, taps,
            tap_count,
#ifdef FAAR_STAGED_SHORT_POLY
            selected->fft == 0 ? 0 : fft_size,
#else
            fft_size,
#endif
            expansion, decimation,
#ifdef FAAR_STAGED_SHORT_POLY
            /* Only Fast meets its mask in float; Good and High need the
             * double rounding floor. */
            quality == FAAR_QUALITY_FAST);
#else
            0);
#endif
    free(taps);
    if (!r->dft_prefilter) { free(r); return NULL; }
#ifdef FAAR_STAGED_SHORT_POLY
    size_t phase_bank_len = plan.bank;
#else
    size_t phase_bank_len = 127;
#endif
    if ((uint64_t)in_rate * expansion > UINT32_MAX) {
        faar_dft_prefilter_destroy(r->dft_prefilter);
        free(r);
        return NULL;
    }
#ifdef FAAR_STAGED_SHORT_POLY
    if (selected->fft == 0) {
        r->delay = (tap_count - 1) / 2;
        return r;
    }
#endif
    uint32_t stage2_rate = in_rate * (uint32_t)expansion
        / (uint32_t)decimation;
    r->stage2 = faar_create_ext_internal(stage2_rate, out_rate, channels,
                                         quality, 0, phase_bank_len, true);
    if (!r->stage2) {
        faar_dft_prefilter_destroy(r->dft_prefilter);
        free(r);
        return NULL;
    }
    r->delay = ((tap_count - 1) / 2 + expansion * decimation - 1)
        / (expansion * decimation)
        + (r->stage2->delay + expansion * decimation - 1)
            / (expansion * decimation);
    return r;
}
#endif

#ifdef FAAR_STAGED_SHORT_POLY
/* Keep the final bank's rate and arithmetic no greater than the original
 * general plan, while moving its sharp filter to the lowest viable rate. */
static unsigned short_max_halvings(uint32_t in, uint32_t out, faar_quality_t q) {
    if (in <= out || short_half_rate_route(in, out)
            || short_dedicated_route(in, out)) return 0;
    struct short_plan original = short_plan(in, out, q, true);
    uint64_t bank = (uint64_t)in * original.expansion / original.decimation;
    unsigned k = 0;
    while (!(in & 1) && in / 2 > out) {
        struct short_plan next = short_plan(in / 2, out, q, true);
        uint64_t rate = (uint64_t)(in / 2) * next.expansion / next.decimation;
        if (rate < 2.5 * out || rate > bank || next.bank > original.bank) break;
        in /= 2;
        ++k;
    }
    return k;
}

/* Coefficients in ns per stereo-independent output, calibrated once from
 * alternating three-round 30 s stereo candidate measurements. Nonnegative
 * least squares rejects calibration choices measured >3% slower than k=0.
 * No rate or preset identities enter this model. */
static double short_work(uint32_t in, uint32_t out, faar_quality_t quality,
        unsigned k, double fir_work, double visited) {
    struct short_plan p = short_plan(in, out, quality, k != 0);
    size_t n = p.fft;
    while (n < p.taps * 5) n *= 2;
    double forward = (double)n / p.expansion;
    double inverse = (double)n / p.decimation;
    double block_input = (double)(n - p.taps + 1) / p.expansion;
    double fft_work = (forward * log2(forward) + inverse * log2(inverse))
        / block_input * in / out;
    return 0.41445 * fir_work + 0.29928 * fft_work + 0.21198 * p.bank
        + 0.74921 * (visited + (double)in / out);
}

static struct short_plan short_preplan(uint32_t in, uint32_t out,
        faar_quality_t quality) {
    struct short_plan pre = {0, 0, 1, 2, 1, 0, 0};
    /* Symmetric edges cover the preset pass edge and reject aliases before
     * they can land inside 1.04 times FINAL output Nyquist. */
    double pb = 0.52 * out / in;
    design_band(quality, pb, 0.5 - pb, 4, 4.7,
                &pre.taps, &pre.beta, &pre.cutoff);
    return pre;
}

static unsigned short_halvings(uint32_t in, uint32_t out, faar_quality_t q) {
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

static faar_handle_t short_cascade(uint32_t in, uint32_t out,
        uint16_t channels, faar_quality_t quality, unsigned k) {
    if (!k) {
        struct short_plan p = short_plan(in, out, quality, true);
        return faar_create_staged(in, out, channels, quality, &p);
    }
    struct short_plan pre = short_preplan(in, out, quality);
    faar_handle_t r = calloc(1, sizeof(*r));
    if (!r) return NULL;
    r->in_rate = in;
    r->out_rate = out;
    r->channels = channels;
    r->quality = quality;
    r->boundary = FAAR_BOUNDARY_HOLD;
    r->ratio = (double)out / in;
    r->stage1 = faar_create_staged(in, in / 2, channels, quality, &pre);
    r->stage2 = short_cascade(in / 2, out, channels, quality, k - 1);
    r->cascade_cap = 4096 * channels;
    r->cascade_buf = aligned_malloc(r->cascade_cap * sizeof(float));
    if (!r->stage1 || !r->stage2 || !r->cascade_buf) {
        faar_destroy(r);
        return NULL;
    }
    r->delay = r->stage1->delay + 2 * r->stage2->delay;
    return r;
}
#endif

static faar_handle_t faar_create_ext_internal(uint32_t in_rate, uint32_t out_rate,
        uint16_t channels, faar_quality_t quality, uint32_t decimate_factor,
        size_t filter_len_override, bool force_polyphase) {
    if (in_rate == 0 || out_rate == 0 || channels == 0 || channels > MAX_CHANNELS) return NULL;

#ifdef FAAR_STAGED_BACKEND
    double rate_ratio = (double)out_rate / (double)in_rate;
    bool downsample_stage = in_rate > out_rate && rate_ratio >= 0.20
        && rate_ratio <= 0.95;
    bool use_staged = downsample_stage;
#ifdef FAAR_STAGED_SHORT_POLY
    use_staged = in_rate > out_rate
        || short_dedicated_route(in_rate, out_rate) != NULL;
#endif
    if (!force_polyphase && use_staged) {
#ifdef FAAR_STAGED_SHORT_POLY
        unsigned k = short_halvings(in_rate, out_rate, quality);
        if (k) return short_cascade(in_rate, out_rate, channels, quality, k);
        struct short_plan p = short_plan(in_rate, out_rate, quality, false);
        return faar_create_staged(in_rate, out_rate, channels, quality, &p);
#else
        return faar_create_staged(in_rate, out_rate, channels, quality);
#endif
    }
#else
    (void)force_polyphase;
#endif

    bool force_decimate = (decimate_factor >= 2) || ((uint64_t)in_rate > 2 * (uint64_t)out_rate);

    /* Multi-stage cascade decimation for large downsampling ratios (>2:1 or explicit decimate_factor) */
    if (force_decimate && in_rate > out_rate && !filter_len_override) {
        uint32_t intermediate_rate = in_rate / (decimate_factor >= 2 ? decimate_factor : 2);
        if (intermediate_rate <= out_rate) intermediate_rate = in_rate / 2;
        if (intermediate_rate <= out_rate) intermediate_rate = (in_rate + out_rate) / 2;

        struct faar_resampler_struct *r = (struct faar_resampler_struct *)calloc(1, sizeof(struct faar_resampler_struct));
        if (!r) return NULL;
        r->boundary = FAAR_BOUNDARY_HOLD;

        r->stage1 = faar_create_ext_internal(in_rate, intermediate_rate,
                channels, quality, 0, 0, true);
        r->stage2 = faar_create_ext_internal(intermediate_rate, out_rate,
                channels, quality, 0, 0, true);

        if (!r->stage1 || !r->stage2) {
            faar_destroy(r->stage1);
            faar_destroy(r->stage2);
            free(r);
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
            faar_destroy(r->stage1);
            faar_destroy(r->stage2);
            free(r);
            return NULL;
        }

        r->delay = r->stage1->delay
            + ((uint64_t)r->stage2->delay * in_rate + intermediate_rate - 1) / intermediate_rate;
        return r;
    }

    struct faar_resampler_struct *r = (struct faar_resampler_struct *)calloc(1, sizeof(struct faar_resampler_struct));
    if (!r) return NULL;
    r->boundary = FAAR_BOUNDARY_HOLD;

    r->in_rate = in_rate;
    r->out_rate = out_rate;
    r->channels = channels;
    r->quality = quality;
    r->ratio = (double)out_rate / (double)in_rate;

    /* Preset lengths and Kaiser beta; rejection depends on the rate and frequency. */
    switch (quality) {
        case FAAR_QUALITY_FAST: r->filter_len = 127; r->beta = 28.0; break;
        case FAAR_QUALITY_HIGH: r->filter_len = 511; r->beta = 38.0; break;
        case FAAR_QUALITY_GOOD:
        default:                r->filter_len = 255; r->beta = 32.0; break;
    }

#ifdef FAAR_STAGED_SHORT_POLY
    if (filter_len_override == 20 || filter_len_override == 21
            || filter_len_override == 41)
        r->beta = 19.60632601597565;
#endif

    if (filter_len_override) {
        r->filter_len = filter_len_override;
        size_t exact_phase_count = 2u * (size_t)(out_rate / rate_gcd(in_rate, out_rate));
        r->num_phases = exact_phase_count <= 4096 ? exact_phase_count : 4096;
        r->phase_gcd = rate_gcd(in_rate, out_rate);
        r->exact_phase_bank = exact_phase_count <= 4096;
        /* The even 20-tap bank needs a half-sample clock origin. */
        if (filter_len_override == 20)
            r->half_sample_phase_bank = true;
    } else r->num_phases = POLYRESAMPLE_NUM_PHASES;

    if ((r->filter_len % 2) == 0
#ifndef FAAR_STAGED_SHORT_POLY
            ) r->filter_len++;
#else
            && filter_len_override != 20) r->filter_len++;
#endif
    assert(r->filter_len <= POLYRESAMPLE_MAX_FILTER_LEN);

    /* Allocate polyphase table with 64-byte SIMD alignment */
    size_t stored_phases = r->num_phases / 2 + 3;
    r->polyphase_table = (polyresample_coef_t *)aligned_malloc(stored_phases * r->filter_len * sizeof(*r->polyphase_table));
    if (!r->polyphase_table) {
        free(r);
        return NULL;
    }
    /* The short exact banks repay a second oriented copy; keep generic banks
       compact and let their uncommon exact reverse phase use the stack path. */
    if (r->exact_phase_bank && r->filter_len <= 32) {
        r->polyphase_reverse_table = (polyresample_coef_t *)aligned_malloc(
                stored_phases * r->filter_len * sizeof(*r->polyphase_reverse_table));
        if (!r->polyphase_reverse_table) {
            aligned_free(r->polyphase_table);
            free(r);
            return NULL;
        }
    }

    double cutoff = (r->ratio < 1.0)
#ifdef FAAR_STAGED_SHORT_POLY
        ? (filter_len_override ? 0.49 : 0.48 * r->ratio) : 0.48;
#else
        ? (filter_len_override ? 0.49 : 0.48 * r->ratio) : 0.48;
#endif
    double half_len = (r->filter_len - 1) / 2.0;

    /* Store phases -1..N/2+1, including guards for cubic interpolation. */
    for (size_t p = 0; p < stored_phases; p++) {
        double frac = ((double)p - 1.0) / (double)r->num_phases;
        polyresample_coef_t *phase_coeffs = r->polyphase_table + p * r->filter_len;
        double sum = 0.0;

        for (size_t j = 0; j < r->filter_len; j++) {
            if (r->filter_len == 1) {
                phase_coeffs[j] = (polyresample_coef_t)1.0;
                sum = 1.0;
                continue;
            }
#ifdef FAAR_STAGED_SHORT_POLY
            bool sampled_long_prototype = filter_len_override == 20
                || (filter_len_override == 21
                    && r->in_rate == 88200 && r->out_rate == 48000);
            if (sampled_long_prototype) {
                /* Independently evaluate the sampled long-prototype geometry
                   used by SoX's 48/96 -> 44.1 kHz plan. This preserves the
                   20-tap even phase origin instead of windowing each short
                   row as if it were an isolated FIR. Parameters are derived
                   from the measured plan; no SoX source or coefficient data
                   is used here. */
                const bool bank_147 = filter_len_override == 20;
                const double prototype_fc = bank_147
                    ? 0.006075233237773342 : 0.011217712407519471;
                const double prototype_phases = bank_147 ? 147.0 : 80.0;
                const double prototype_center = bank_147 ? 1469.0 : 839.0;
                const double prototype_denominator = bank_147 ? 1469.75 : 839.75;
                const double prototype_scale = bank_147 ? 147.0 : 80.0;
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
                    : bessel_i0(r->beta * sqrt(radicand)) / bessel_i0(r->beta);
                double val = sinc * win * prototype_scale;
                phase_coeffs[j] = (polyresample_coef_t)val;
                sum += val;
                continue;
            }
#endif
            double n = (double)j - half_len - frac;
            double sinc = (fabs(n) < 1e-9) ? (2.0 * cutoff) : sin(2.0 * M_PI * cutoff * n) / (M_PI * n);
#ifdef FAAR_STAGED_SHORT_POLY
            double window_position = (double)j;
            if (filter_len_override == 21 && r->in_rate == 48000
                    && r->out_rate == 32000) {
                /* Move the Kaiser window with the fractional-delay center.
                 * This makes complementary phases time-reversal symmetric. */
                window_position -= frac;
            }
#else
            double window_position = (double)j;
#endif
            double win = kaiser_window(window_position,
                                       (double)r->filter_len - 1.0, r->beta);
            double val = sinc * win;
            phase_coeffs[j] = (polyresample_coef_t)val;
            sum += val;
        }

        bool normalize_phase = true;
#ifdef FAAR_STAGED_SHORT_POLY
        bool sampled_long_prototype = filter_len_override == 20
            || (filter_len_override == 21
                && r->in_rate == 88200 && r->out_rate == 48000);
        if (sampled_long_prototype)
            normalize_phase = false;
#endif
        if (normalize_phase && sum > 0.0) {
            for (size_t j = 0; j < r->filter_len; j++) {
#ifdef FAAR_DOUBLE_COEFFICIENTS
                phase_coeffs[j] = (polyresample_coef_t)(phase_coeffs[j] / sum);
#else
                phase_coeffs[j] /= (float)sum;
#endif
            }
        }
    }
    if (r->polyphase_reverse_table) {
        for (size_t p = 0; p < stored_phases; ++p) {
            const polyresample_coef_t *source =
                r->polyphase_table + p * r->filter_len;
            polyresample_coef_t *reverse =
                r->polyphase_reverse_table + p * r->filter_len;
            for (size_t j = 0; j < r->filter_len; ++j)
                reverse[j] = source[r->filter_len - 1 - j];
        }
    }

    /* Keep each planar history aligned to 64 bytes for vector-friendly strides. */
    r->hist_cap_per_ch = (r->filter_len * 2 + 4096 + 15) & ~((size_t)15);

    for (uint16_t c = 0; c < channels; c++) {
        r->hist[c] = (polyresample_coef_t *)aligned_malloc(
                r->hist_cap_per_ch * sizeof(*r->hist[c]));
        if (!r->hist[c]) {
            for (uint16_t k = 0; k < c; k++) aligned_free(r->hist[k]);
            aligned_free(r->polyphase_reverse_table);
            aligned_free(r->polyphase_table);
            free(r);
            return NULL;
        }
        memset(r->hist[c], 0, r->hist_cap_per_ch * sizeof(*r->hist[c]));
    }

    r->delay = in_rate == out_rate ? 0 : (size_t)half_len;
    /* Center the first output on input frame zero. */
    r->history_pad = r->delay;
#ifdef FAAR_STAGED_SHORT_POLY
    if (filter_len_override == 20) {
        r->history_pad++;
        r->phase = out_rate / 2;
    }
#endif
    r->hist_len = in_rate == out_rate ? 0 : r->history_pad;
    return r;
}

FAARAPI faar_handle_t faar_create_ext(uint32_t in_rate, uint32_t out_rate,
        uint16_t channels, faar_quality_t quality, uint32_t decimate_factor) {
    return faar_create_ext_internal(in_rate, out_rate, channels, quality,
                                    decimate_factor, 0, false);
}

FAARAPI int faar_set_boundary(faar_handle_t r, faar_boundary_t boundary) {
    if (!r || (boundary != FAAR_BOUNDARY_ZERO && boundary != FAAR_BOUNDARY_HOLD)
        || r->input_frames || r->flushed) return -1;
#ifdef FAAR_STAGED_BACKEND
    if (r->dft_prefilter
            && faar_dft_prefilter_set_boundary(r->dft_prefilter,
                                               boundary == FAAR_BOUNDARY_HOLD))
        return -1;
#endif
    if (r->stage1 && (faar_set_boundary(r->stage1, boundary)
                  || faar_set_boundary(r->stage2, boundary))) return -1;
    if (!r->stage1 && r->stage2 && faar_set_boundary(r->stage2, boundary)) return -1;
    r->boundary = boundary;
    return 0;
}

FAARAPI int faar_set_dither(faar_handle_t r, faar_dither_t dither, uint8_t target_bits) {
    if (!r) return -1;
    if (dither != FAAR_DITHER_OFF && target_bits != 16 && target_bits != 24) return -1;

    if (r->stage2) {
        return faar_set_dither(r->stage2, dither, target_bits);
    }

    r->dither = dither;
    r->dither_bits = target_bits;
    for (uint16_t c = 0; c < r->channels && c < MAX_CHANNELS; c++) {
        r->prng_state[c] = (uint32_t)(123456789U + c * 987654321U);
        r->error_history[c] = 0.0f;
    }
    return 0;
}

FAARAPI size_t faar_get_delay(faar_handle_t r) {
    return r ? r->delay : 0;
}

static size_t polyresample_process_single(faar_handle_t restrict r, const float * restrict in_samples, size_t in_count,
                                           float * restrict out_samples, size_t max_out_count) {
    if (!r || !out_samples) return 0;

    const size_t filter_len = r->filter_len;
    const size_t half_len = (filter_len - 1) / 2;
    const polyresample_coef_t * restrict poly_table = r->polyphase_table + filter_len;

    assert(filter_len <= POLYRESAMPLE_MAX_FILTER_LEN);

    if (in_samples && in_count > 0) {
        size_t needed_cap = r->hist_len + in_count;
        if (needed_cap > r->hist_cap_per_ch) {
            size_t new_cap = (needed_cap * 2 + 15) & ~((size_t)15);

            for (uint16_t c = 0; c < r->channels; c++) {
                polyresample_coef_t *new_buf = (polyresample_coef_t *)aligned_malloc(
                        new_cap * sizeof(*new_buf));
                if (!new_buf) return 0;
                memcpy(new_buf, r->hist[c], r->hist_len * sizeof(*new_buf));
                aligned_free(r->hist[c]);
                r->hist[c] = new_buf;
            }
            r->hist_cap_per_ch = new_cap;
        }

        /* Fast unit-stride de-interleaving for mono, stereo, and 5.1 / 7.1 multichannel streams */
        if (r->channels == 2) {
            polyresample_coef_t * restrict h0 = r->hist[0] + r->hist_len;
            polyresample_coef_t * restrict h1 = r->hist[1] + r->hist_len;
            for (size_t i = 0; i < in_count; i++) {
                h0[i] = (polyresample_coef_t)in_samples[i * 2 + 0];
                h1[i] = (polyresample_coef_t)in_samples[i * 2 + 1];
            }
        } else if (r->channels == 6) {
            /* 5.1 Surround Fast De-Interleave Path */
            polyresample_coef_t * restrict h0 = r->hist[0] + r->hist_len;
            polyresample_coef_t * restrict h1 = r->hist[1] + r->hist_len;
            polyresample_coef_t * restrict h2 = r->hist[2] + r->hist_len;
            polyresample_coef_t * restrict h3 = r->hist[3] + r->hist_len;
            polyresample_coef_t * restrict h4 = r->hist[4] + r->hist_len;
            polyresample_coef_t * restrict h5 = r->hist[5] + r->hist_len;
            for (size_t i = 0; i < in_count; i++) {
                h0[i] = (polyresample_coef_t)in_samples[i * 6 + 0];
                h1[i] = (polyresample_coef_t)in_samples[i * 6 + 1];
                h2[i] = (polyresample_coef_t)in_samples[i * 6 + 2];
                h3[i] = (polyresample_coef_t)in_samples[i * 6 + 3];
                h4[i] = (polyresample_coef_t)in_samples[i * 6 + 4];
                h5[i] = (polyresample_coef_t)in_samples[i * 6 + 5];
            }
        } else if (r->channels == 1) {
            for (size_t i = 0; i < in_count; ++i)
                r->hist[0][r->hist_len + i] = (polyresample_coef_t)in_samples[i];
        } else {
            for (size_t i = 0; i < in_count; i++) {
                for (uint16_t c = 0; c < r->channels; c++) {
                    r->hist[c][r->hist_len + i] =
                        (polyresample_coef_t)in_samples[i * r->channels + c];
                }
            }
        }
        /* Initialize the virtual past only once, independently of chunk size. */
        if (!r->input_frames && !r->flushed && r->boundary == FAAR_BOUNDARY_HOLD) {
            for (uint16_t c = 0; c < r->channels; c++) {
            polyresample_coef_t first = r->hist[c][r->history_pad];
                for (size_t j = 0; j < r->history_pad; j++) r->hist[c][j] = first;
            }
        }
        r->hist_len += in_count;
        if (!r->flushed) r->input_frames += in_count;
    }

    max_out_count = output_capacity(r, max_out_count);
    if (r->in_rate == r->out_rate) {
        size_t frames = r->hist_len < max_out_count ? r->hist_len : max_out_count;
        for (uint16_t c = 0; c < r->channels; c++) {
            for (size_t j = 0; j < frames; j++) {
                out_samples[j * r->channels + c] = (float)r->hist[c][j];
            }
            memmove(r->hist[c], r->hist[c] + frames,
                    (r->hist_len - frames) * sizeof(*r->hist[c]));
        }
        r->hist_len -= frames;
        r->output_frames += frames;
        apply_dither(r, out_samples, frames);
        return frames;
    }

    size_t out_frames = 0;
    /* Split the rational clock once per call. Updating quotient and remainder
       per output avoids a 64-bit divide/modulo pair in the sample loop. */
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

    /* Stack-allocated, 64-byte aligned temporary coefficient array for SIMD auto-vectorization */
#if defined(_MSC_VER)
    __declspec(align(64)) float temp_coeffs[POLYRESAMPLE_MAX_FILTER_LEN];
#else
    alignas(64) polyresample_coef_t temp_coeffs[POLYRESAMPLE_MAX_FILTER_LEN];
#endif

    while (out_frames < max_out_count) {
        size_t sample_base_idx = base_idx;
        double frac = 0.0;
        bool reverse;
        size_t p_idx;
        double delta;
        if (r->exact_phase_bank) {
            /* The doubled grid represents the 20-tap bank's half-sample
               origin; all other exact banks use the ordinary reduced grid. */
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
                delta = 0.0;
            } else {
                p_idx = r->num_phases / 2 - 1;
                delta = 1.0;
            }
        } else {
            frac = (double)phase_remainder / r->out_rate;
            /* Reflection changes the sign of fractional delay. Anchor negative
               phases at the next input sample so the evaluated position agrees. */
            reverse = frac > 0.5;
            if (reverse) {
                sample_base_idx++;
                frac = 1.0 - frac;
            }
            double phase_exact = frac * (double)r->num_phases;
            p_idx = (size_t)floor(phase_exact);
            if (p_idx >= r->num_phases / 2)
                p_idx = r->num_phases / 2 - 1;
            delta = phase_exact - (double)p_idx;
        }
        if (sample_base_idx + filter_len > r->hist_len) {
            break;
        }

        if ((filter_len & 1) && p_idx == 0 && delta < 1e-6) {
            /* Fast path for phase 0 (exact integer alignment): fold symmetric taps */
            const polyresample_coef_t * restrict coeffs = poly_table;
            const polyresample_coef_t center_coeff = coeffs[half_len];

            for (uint16_t c = 0; c < r->channels; c++) {
                const polyresample_coef_t * restrict h = &r->hist[c][sample_base_idx];
                double sum = (double)center_coeff * h[half_len];

                for (size_t j = 0; j < half_len; j++) {
                    sum += (double)coeffs[j] * ((double)h[j] + h[filter_len - 1 - j]);
                }

                out_samples[out_frames * r->channels + c] = sum;
            }
        } else {
            /* Interpolate-Then-Convolve: Pre-calculate flat interpolated filter coefficients outside channel/tap loops */
            const polyresample_coef_t * restrict c1 = poly_table + p_idx * filter_len;
            const polyresample_coef_t * restrict c0 = c1 - filter_len;
            const polyresample_coef_t * restrict c2 = c1 + filter_len;
            const polyresample_coef_t * restrict c3 = c2 + filter_len;
            const polyresample_coef_t * restrict coeffs;

            /* Exact rational phases need no four-row interpolation. */
            if (delta < 1e-6) {
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
               a smaller phase bank without the leading cubic phase error. */
#ifdef FAAR_DOUBLE_COEFFICIENTS
            double d = delta;
            double d2 = d * d;
            double d3 = d2 * d;
            double w0 = (-d3 + 3.0 * d2 - 2.0 * d) / 6.0;
            double w1 = (d3 - 2.0 * d2 - d + 2.0) / 2.0;
            double w2 = (-d3 + d2 + 2.0 * d) / 2.0;
            double w3 = (d3 - d) / 6.0;
#else
            float d = (float)delta;
            float d2 = d * d;
            float d3 = d2 * d;
            float w0 = (-d3 + 3.0f * d2 - 2.0f * d) / 6.0f;
            float w1 = (d3 - 2.0f * d2 - d + 2.0f) / 2.0f;
            float w2 = (-d3 + d2 + 2.0f * d) / 2.0f;
            float w3 = (d3 - d) / 6.0f;
#endif

            for (size_t j = 0; j < filter_len; j++) {
                size_t target = reverse ? filter_len - 1 - j : j;
                temp_coeffs[target] = (polyresample_coef_t)(w0 * c0[j] + w1 * c1[j] + w2 * c2[j] + w3 * c3[j]);
            }
                coeffs = temp_coeffs;
            }

            /* Double accumulation keeps rounding noise below the float coefficient floor. */
            for (uint16_t c = 0; c < r->channels; c++) {
                const polyresample_coef_t * restrict h = &r->hist[c][sample_base_idx];
                double accumulator0 = 0.0, accumulator1 = 0.0;
                double accumulator2 = 0.0, accumulator3 = 0.0;
                size_t j = 0;
                for (; j + 3 < filter_len; j += 4) {
                    accumulator0 += (double)h[j] * coeffs[j];
                    accumulator1 += (double)h[j + 1] * coeffs[j + 1];
                    accumulator2 += (double)h[j + 2] * coeffs[j + 2];
                    accumulator3 += (double)h[j + 3] * coeffs[j + 3];
                }
                double accumulator = accumulator0 + accumulator1
                    + accumulator2 + accumulator3;
                for (; j < filter_len; ++j)
                    accumulator += (double)h[j] * coeffs[j];
                out_samples[out_frames * r->channels + c] = (float)accumulator;
            }
        }

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
            memmove(r->hist[c], r->hist[c] + discard,
                    (r->hist_len - discard) * sizeof(*r->hist[c]));
        r->hist_len -= discard;
        r->phase -= (uint64_t)discard * r->out_rate;
    }

    apply_dither(r, out_samples, out_frames);
    return out_frames;
}

FAARAPI size_t faar_process(faar_handle_t restrict r, const float * restrict in_samples, size_t in_count,
                           float * restrict out_samples, size_t max_out_count) {
    if (!r || !out_samples || (r->flushed && in_samples && in_count)) return 0;

#ifdef FAAR_STAGED_BACKEND
    if (r->dft_prefilter) {
        if (in_count > (SIZE_MAX - 8192) / r->dft_expansion
                || in_count * r->dft_expansion + 8192
                    > SIZE_MAX / r->channels / sizeof(float))
            return 0;
        size_t temp_capacity = in_count * r->dft_expansion + 8192;
        if (ensure_dft_intermediate(r, temp_capacity)) return 0;
        size_t filtered = 0;
        if (in_samples) r->input_frames += in_count;
        if (faar_dft_prefilter_process(r->dft_prefilter, in_samples, in_count,
                r->dft_intermediate, r->stage2 ? temp_capacity
                    : (max_out_count < temp_capacity ? max_out_count : temp_capacity),
                &filtered)) return 0;
        size_t produced;
        if (r->stage2) {
            produced = polyresample_process_planar(r->stage2,
                    r->dft_intermediate, filtered, out_samples, max_out_count);
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
#endif

    if (r->stage1 && r->stage2) {
        size_t produced = 0, stage1_produced;
        size_t capacity = r->cascade_cap / r->channels;
        if (in_samples) r->input_frames += in_count;
        max_out_count = output_capacity(r, max_out_count);
        do {
            stage1_produced = faar_process(r->stage1, in_samples, in_count, r->cascade_buf, capacity);
            produced += faar_process(r->stage2, r->cascade_buf, stage1_produced,
                                     out_samples + produced * r->channels, max_out_count - produced);
            in_samples = NULL;
            in_count = 0;
        } while (stage1_produced == capacity);
        r->output_frames += produced;
        return produced;
    }

    return polyresample_process_single(r, in_samples, in_count, out_samples, max_out_count);
}

#ifdef FAAR_STAGED_BACKEND
static size_t polyresample_process_planar(faar_handle_t r,
        const float * restrict in_samples, size_t in_count,
        float * restrict out_samples, size_t max_out_count) {
    if (!r || !out_samples || (in_count && !in_samples)) return 0;
    if (in_count) {
        if (in_count > SIZE_MAX - r->hist_len) return 0;
        size_t needed_cap = r->hist_len + in_count;
        if (needed_cap > r->hist_cap_per_ch) {
            if (needed_cap > (SIZE_MAX - 15) / 2) return 0;
            size_t new_cap = (needed_cap * 2 + 15) & ~((size_t)15);
            for (uint16_t c = 0; c < r->channels; ++c) {
                polyresample_coef_t *new_buf = (polyresample_coef_t *)aligned_malloc(
                        new_cap * sizeof(*new_buf));
                if (!new_buf) return 0;
                memcpy(new_buf, r->hist[c], r->hist_len * sizeof(*new_buf));
                aligned_free(r->hist[c]);
                r->hist[c] = new_buf;
            }
            r->hist_cap_per_ch = new_cap;
        }
        for (uint16_t c = 0; c < r->channels; ++c) {
            const float * restrict channel_input = in_samples + (size_t)c * in_count;
            polyresample_coef_t * restrict channel_history =
                r->hist[c] + r->hist_len;
#ifdef FAAR_DOUBLE_COEFFICIENTS
            for (size_t i = 0; i < in_count; ++i)
                channel_history[i] = channel_input[i];
#else
            memcpy(channel_history, channel_input,
                   in_count * sizeof(*channel_history));
#endif
        }
        if (!r->input_frames && !r->flushed
                && r->boundary == FAAR_BOUNDARY_HOLD) {
            for (uint16_t c = 0; c < r->channels; ++c) {
                polyresample_coef_t first = r->hist[c][r->history_pad];
                for (size_t j = 0; j < r->history_pad; ++j)
                    r->hist[c][j] = first;
            }
        }
        r->hist_len += in_count;
        if (!r->flushed) r->input_frames += in_count;
    }
    return polyresample_process_single(r, NULL, 0, out_samples, max_out_count);
}
#endif

static size_t polyresample_flush_single(faar_handle_t r, float *out_samples, size_t max_out_count) {
    if (!r || !out_samples) return 0;

    if (r->in_rate == r->out_rate) {
        r->flushed = true;
        return polyresample_process_single(r, NULL, 0, out_samples, max_out_count);
    }
    if (!r->flushed) {
        size_t tail_len = (r->filter_len - 1) / 2 + 1;
#ifdef FAAR_STAGED_SHORT_POLY
        if (r->filter_len == 20) ++tail_len;
#endif
        float *tail = (float *)malloc(tail_len * r->channels * sizeof(float));
        if (tail) {
            for (uint16_t c = 0; c < r->channels; c++) {
                polyresample_coef_t last = r->boundary == FAAR_BOUNDARY_HOLD && r->input_frames
                    ? r->hist[c][r->hist_len - 1] : 0.0f;
                for (size_t j = 0; j < tail_len; j++) tail[j * r->channels + c] = last;
            }
            r->flushed = true;
            size_t written = polyresample_process_single(r, tail, tail_len, out_samples, max_out_count);
            free(tail);
            return written;
        }
    }

    return polyresample_process_single(r, NULL, 0, out_samples, max_out_count);
}

FAARAPI size_t faar_flush(faar_handle_t r, float *out_samples, size_t max_out_count) {
    if (!r || !out_samples) return 0;

#ifdef FAAR_STAGED_BACKEND
    if (r->dft_prefilter) {
        r->flushed = true;
        max_out_count = output_capacity(r, max_out_count);
        const size_t temp_capacity = 8192;
        if (ensure_dft_intermediate(r, temp_capacity)) return 0;
        size_t produced = 0, filtered;
        if (!r->stage2) {
            size_t capacity = max_out_count < temp_capacity ? max_out_count : temp_capacity;
            if (faar_dft_prefilter_flush(r->dft_prefilter, r->dft_intermediate,
                        capacity, &filtered)) return 0;
            for (size_t i = 0; i < filtered; ++i)
                for (uint16_t c = 0; c < r->channels; ++c)
                    out_samples[i * r->channels + c] = r->dft_intermediate[c * filtered + i];
            r->output_frames += filtered;
            apply_dither(r, out_samples, filtered);
            return filtered;
        }
        do {
            filtered = 0;
            if (faar_dft_prefilter_flush(r->dft_prefilter, r->dft_intermediate,
                    temp_capacity, &filtered)) {
                return 0;
            }
            produced += polyresample_process_planar(r->stage2, r->dft_intermediate,
                    filtered, out_samples + produced * r->channels,
                    max_out_count - produced);
        } while (filtered == temp_capacity);
        produced += faar_flush(r->stage2,
                out_samples + produced * r->channels,
                max_out_count - produced);
        r->output_frames += produced;
        return produced;
    }
#endif

    if (r->stage1 && r->stage2) {
        size_t produced = 0, stage1_flushed;
        size_t capacity = r->cascade_cap / r->channels;
        r->flushed = true;
        max_out_count = output_capacity(r, max_out_count);
        do {
            stage1_flushed = faar_flush(r->stage1, r->cascade_buf, capacity);
            produced += faar_process(r->stage2, r->cascade_buf, stage1_flushed,
                                     out_samples + produced * r->channels, max_out_count - produced);
        } while (stage1_flushed == capacity);
        produced += faar_flush(r->stage2, out_samples + produced * r->channels, max_out_count - produced);
        r->output_frames += produced;
        return produced;
    }

    return polyresample_flush_single(r, out_samples, max_out_count);
}

FAARAPI void faar_destroy(faar_handle_t r) {
    if (!r) return;
    if (r->stage1) faar_destroy(r->stage1);
    if (r->stage2) faar_destroy(r->stage2);
#ifdef FAAR_STAGED_BACKEND
    if (r->dft_prefilter) faar_dft_prefilter_destroy(r->dft_prefilter);
    free(r->dft_intermediate);
#endif
    if (r->cascade_buf) aligned_free(r->cascade_buf);
    for (uint16_t c = 0; c < r->channels && c < MAX_CHANNELS; c++) {
        if (r->hist[c]) aligned_free(r->hist[c]);
    }
    if (r->polyphase_reverse_table) aligned_free(r->polyphase_reverse_table);
    if (r->polyphase_table) aligned_free(r->polyphase_table);
    free(r);
}
