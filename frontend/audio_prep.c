/*
 * FAAC - Freeware Advanced Audio Coder
 * Audio Preprocessing Pipeline (xHE-AAC inspired low-bitrate acoustic prep)
 */

#include "audio_prep.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Biquad Direct Form II Transposed */
typedef struct {
    float b0, b1, b2;
    float a1, a2;
    float d1, d2;
} biquad_filter_t;

static inline float biquad_process(biquad_filter_t *b, float in)
{
    float out = in * b->b0 + b->d1;
    b->d1 = in * b->b1 - out * b->a1 + b->d2;
    b->d2 = in * b->b2 - out * b->a2;
    return out;
}

/* High-Shelf Filter via Bilinear Transform */
static void biquad_set_highshelf(biquad_filter_t *b, float rate, float freq, float gain_db, float S)
{
    (void)S;
    if (gain_db == 0.0f || freq >= rate * 0.49f) {
        b->b0 = 1.0f; b->b1 = 0.0f; b->b2 = 0.0f;
        b->a1 = 0.0f; b->a2 = 0.0f;
        return;
    }
    double fs = (double)rate;
    double f0 = (double)freq;
    double Q = 0.70710678;
    double V0 = pow(10.0, (double)gain_db / 20.0);
    double sv = sqrt(V0);
    double K = tan(M_PI * f0 / fs);
    double d = 1.0 + (1.0 / Q) * K + K * K;

    b->b0 = (float)((V0 + sv * (1.0 / Q) * K + K * K) / d);
    b->b1 = (float)((2.0 * (K * K - V0)) / d);
    b->b2 = (float)((V0 - sv * (1.0 / Q) * K + K * K) / d);
    b->a1 = (float)((2.0 * (K * K - 1.0)) / d);
    b->a2 = (float)((1.0 - (1.0 / Q) * K + K * K) / d);
}

/* Butterworth 2nd-order Low-Pass Filter */
static void biquad_set_lowpass(biquad_filter_t *b, float rate, float freq)
{
    if (freq >= rate * 0.49f) {
        b->b0 = 1.0f; b->b1 = 0.0f; b->b2 = 0.0f;
        b->a1 = 0.0f; b->a2 = 0.0f;
        return;
    }
    float w0 = 2.0f * (float)M_PI * freq / rate;
    float cos_w0 = cosf(w0);
    float sin_w0 = sinf(w0);
    float alpha = sin_w0 / (2.0f * 0.70710678f);

    float a0 = 1.0f + alpha;
    b->b0 = ((1.0f - cos_w0) / 2.0f) / a0;
    b->b1 = (1.0f - cos_w0) / a0;
    b->b2 = ((1.0f - cos_w0) / 2.0f) / a0;
    b->a1 = (-2.0f * cos_w0) / a0;
    b->a2 = (1.0f - alpha) / a0;
}

/* Bandpass filter for mid-frequency stereo narrowing (1 kHz - 5 kHz) */
static void biquad_set_bandpass(biquad_filter_t *b, float rate, float f0, float Q)
{
    float w0 = 2.0f * (float)M_PI * f0 / rate;
    float cos_w0 = cosf(w0);
    float sin_w0 = sinf(w0);
    float alpha = sin_w0 / (2.0f * Q);

    float a0 = 1.0f + alpha;
    b->b0 = (sin_w0 / 2.0f) / a0;
    b->b1 = 0.0f;
    b->b2 = (-sin_w0 / 2.0f) / a0;
    b->a1 = (-2.0f * cos_w0) / a0;
    b->a2 = (1.0f - alpha) / a0;
}

#define MAX_CHANNELS 8

struct audio_prep_pipeline {
    uint32_t sample_rate;
    uint16_t channels;
    uint32_t bitrate_per_channel;

    /* Resolved parameters */
    float shelf_gain_db;
    float transient_tame_db;
    float mono_cutoff_hz;
    float stereo_mid_narrow;
    float dynamics_amount;

    /* Stage 1: High-shelf pre-emphasis & HF limiter */
    biquad_filter_t shelf_filter[MAX_CHANNELS];

    /* Stage 2: Micro-transient softening (Zero-Latency) */
    float fast_envelope[MAX_CHANNELS];
    float slow_envelope[MAX_CHANNELS];
    float transient_gain[MAX_CHANNELS];

    /* Stage 3: Stereo width management (channels == 2) */
    biquad_filter_t bass_lowpass_left;
    biquad_filter_t bass_lowpass_right;
    biquad_filter_t mid_bandpass_side;

    /* Stage 4: Upward dynamics target compression */
    float dynamics_env[MAX_CHANNELS];
};

void audio_prep_config_init(audio_prep_config_t *cfg)
{
    if (!cfg) return;
    cfg->mode = AUDIO_PREP_OFF;      /* Default OFF for bit-exact reference parity */
    cfg->shelf_gain_db = -1.0f;      /* auto */
    cfg->transient_tame_db = -1.0f;  /* auto */
    cfg->mono_cutoff_hz = -1.0f;     /* auto */
    cfg->stereo_mid_narrow = -1.0f;  /* auto */
    cfg->dynamics_target_db = -18.0f;
    cfg->dynamics_amount = -1.0f;    /* auto */
}

audio_prep_pipeline_t *audio_prep_create(const audio_prep_config_t *cfg,
                                         uint32_t sample_rate,
                                         uint16_t channels,
                                         uint32_t bitrate_per_channel)
{
    if (!cfg || cfg->mode == AUDIO_PREP_OFF || channels == 0 || channels > MAX_CHANNELS) {
        return NULL;
    }

    audio_prep_pipeline_t *pipe = (audio_prep_pipeline_t *)calloc(1, sizeof(audio_prep_pipeline_t));
    if (!pipe) return NULL;

    pipe->sample_rate = sample_rate;
    pipe->channels = channels;
    pipe->bitrate_per_channel = bitrate_per_channel;

    /* Bitrate scaling:
     * Full strength at <= 20000 bps/ch (e.g. 40 kbps stereo).
     * Linear taper to 0.0 at >= 48000 bps/ch (e.g. 96 kbps stereo).
     */
    float br_factor = 0.0f;
    if (bitrate_per_channel <= 20000) {
        br_factor = 1.0f;
    } else if (bitrate_per_channel < 48000) {
        br_factor = (float)(48000 - bitrate_per_channel) / (float)(48000 - 20000);
    } else {
        br_factor = 0.0f;
    }

    if (cfg->mode == AUDIO_PREP_ON) {
        if (br_factor < 0.5f) br_factor = 0.5f;
    }

    /* Stage 1: High shelf (+3.0 dB scaled) */
    pipe->shelf_gain_db = (cfg->shelf_gain_db >= 0.0f) ? cfg->shelf_gain_db : (3.0f * br_factor);
    for (uint16_t c = 0; c < channels; c++) {
        biquad_set_highshelf(&pipe->shelf_filter[c], (float)sample_rate, 4000.0f, pipe->shelf_gain_db, 1.0f);
    }

    /* Stage 2: Transient tame (2.5 dB scaled) */
    pipe->transient_tame_db = (cfg->transient_tame_db >= 0.0f) ? cfg->transient_tame_db : (2.5f * br_factor);
    for (uint16_t c = 0; c < channels; c++) {
        pipe->transient_gain[c] = 1.0f;
    }

    /* Stage 3: Stereo width management (Stereo only) */
    if (channels == 2) {
        pipe->mono_cutoff_hz = (cfg->mono_cutoff_hz >= 0.0f) ? cfg->mono_cutoff_hz : (220.0f * br_factor);
        pipe->stereo_mid_narrow = (cfg->stereo_mid_narrow >= 0.0f) ? cfg->stereo_mid_narrow : (0.15f * br_factor);

        if (pipe->mono_cutoff_hz > 20.0f) {
            biquad_set_lowpass(&pipe->bass_lowpass_left, (float)sample_rate, pipe->mono_cutoff_hz);
            biquad_set_lowpass(&pipe->bass_lowpass_right, (float)sample_rate, pipe->mono_cutoff_hz);
        }
        if (pipe->stereo_mid_narrow > 0.01f) {
            biquad_set_bandpass(&pipe->mid_bandpass_side, (float)sample_rate, 2500.0f, 0.8f);
        }
    }

    /* Stage 4: Upward dynamics target */
    pipe->dynamics_amount = (cfg->dynamics_amount >= 0.0f) ? cfg->dynamics_amount : (0.25f * br_factor);

    if (cfg->mode == AUDIO_PREP_AUTO &&
        pipe->shelf_gain_db < 0.05f &&
        pipe->transient_tame_db < 0.05f &&
        pipe->mono_cutoff_hz < 10.0f &&
        pipe->stereo_mid_narrow < 0.01f &&
        pipe->dynamics_amount < 0.01f) {
        free(pipe);
        return NULL;
    }

    return pipe;
}

void audio_prep_process(audio_prep_pipeline_t *pipe,
                        float *samples,
                        size_t num_frames)
{
    if (!pipe || !samples || num_frames == 0) return;

    const uint16_t chs = pipe->channels;
    const float dt = 1.0f / (float)pipe->sample_rate;

    /* Coefficients for Stage 2 (Zero-latency transient peak tamer) */
    const float fast_release_coeff = expf(-dt / 0.005f);  /* 5 ms */
    const float slow_attack_coeff = expf(-dt / 0.020f);   /* 20 ms */
    const float slow_release_coeff = expf(-dt / 0.050f);  /* 50 ms */
    const float max_tame_linear = powf(10.0f, -pipe->transient_tame_db / 20.0f);

    /* Coefficients for Stage 4 */
    const float dyn_attack_coeff = expf(-dt / 0.010f);    /* 10 ms */
    const float dyn_release_coeff = expf(-dt / 0.150f);   /* 150 ms */

    for (size_t i = 0; i < num_frames; i++) {
        float *frame = &samples[i * chs];

        /* ================================================================
         * Stage 1: High-Shelf Pre-Emphasis & Soft Limiting
         * ================================================================ */
        if (pipe->shelf_gain_db > 0.05f) {
            for (uint16_t c = 0; c < chs; c++) {
                float s = biquad_process(&pipe->shelf_filter[c], frame[c]);
                const float threshold = 0.92f;
                if (fabsf(s) > threshold) {
                    float sign = (s > 0.0f) ? 1.0f : -1.0f;
                    float excess = fabsf(s) - threshold;
                    s = sign * (threshold + tanhf(excess / (threshold * 0.5f)) * (threshold * 0.15f));
                }
                frame[c] = s;
            }
        }

        /* ================================================================
         * Stage 2: Micro-Transient Softening (Zero Latency)
         * ================================================================ */
        if (pipe->transient_tame_db > 0.05f) {
            for (uint16_t c = 0; c < chs; c++) {
                float in_val = frame[c];
                float abs_val = fabsf(in_val);

                /* Fast peak detector: instantaneous on rise, exponential decay */
                if (abs_val > pipe->fast_envelope[c])
                    pipe->fast_envelope[c] = abs_val;
                else
                    pipe->fast_envelope[c] = abs_val + fast_release_coeff * (pipe->fast_envelope[c] - abs_val);

                /* Slow envelope tracking body energy */
                if (abs_val > pipe->slow_envelope[c])
                    pipe->slow_envelope[c] = abs_val + slow_attack_coeff * (pipe->slow_envelope[c] - abs_val);
                else
                    pipe->slow_envelope[c] = abs_val + slow_release_coeff * (pipe->slow_envelope[c] - abs_val);

                /* Transient ratio: sharp attack if fast peak >> slow body energy */
                float ratio = (pipe->fast_envelope[c] + 1e-4f) / (pipe->slow_envelope[c] + 1e-4f);
                float target_gain = 1.0f;
                if (ratio > 1.8f) {
                    float excess = ratio - 1.8f;
                    float reduction = excess / (excess + 2.0f);
                    target_gain = 1.0f - reduction * (1.0f - max_tame_linear);
                }

                if (target_gain < pipe->transient_gain[c])
                    pipe->transient_gain[c] = target_gain;
                else
                    pipe->transient_gain[c] += 0.05f * (target_gain - pipe->transient_gain[c]);

                frame[c] = in_val * pipe->transient_gain[c];
            }
        }

        /* ================================================================
         * Stage 3: Sub-Band Stereo Width Management (Stereo Only)
         * ================================================================ */
        if (chs == 2 && (pipe->mono_cutoff_hz > 20.0f || pipe->stereo_mid_narrow > 0.01f)) {
            float left = frame[0];
            float right = frame[1];

            /* Sub-band bass mono summing */
            if (pipe->mono_cutoff_hz > 20.0f) {
                float left_bass = biquad_process(&pipe->bass_lowpass_left, left);
                float right_bass = biquad_process(&pipe->bass_lowpass_right, right);
                float mono_bass = 0.5f * (left_bass + right_bass);

                left = left - left_bass + mono_bass;
                right = right - right_bass + mono_bass;
            }

            /* Mid-range stereo narrowing (1 kHz - 5 kHz) */
            if (pipe->stereo_mid_narrow > 0.01f) {
                float mid = 0.5f * (left + right);
                float side = 0.5f * (left - right);

                float side_mid_band = biquad_process(&pipe->mid_bandpass_side, side);
                side -= side_mid_band * pipe->stereo_mid_narrow;

                left = mid + side;
                right = mid - side;
            }

            frame[0] = left;
            frame[1] = right;
        }

        /* ================================================================
         * Stage 4: Upward Dynamics Target Compression
         * ================================================================ */
        if (pipe->dynamics_amount > 0.01f) {
            for (uint16_t c = 0; c < chs; c++) {
                float s = frame[c];
                float abs_val = fabsf(s);

                if (abs_val > pipe->dynamics_env[c])
                    pipe->dynamics_env[c] = abs_val + dyn_attack_coeff * (pipe->dynamics_env[c] - abs_val);
                else
                    pipe->dynamics_env[c] = abs_val + dyn_release_coeff * (pipe->dynamics_env[c] - abs_val);

                if (pipe->dynamics_env[c] > 0.001f && pipe->dynamics_env[c] < 0.25f) {
                    float lift = (0.25f - pipe->dynamics_env[c]) / 0.25f;
                    float gain = 1.0f + pipe->dynamics_amount * lift * 0.25f;
                    frame[c] = s * gain;
                }
            }
        }
    }
}

void audio_prep_free(audio_prep_pipeline_t *pipe)
{
    if (pipe) {
        free(pipe);
    }
}
