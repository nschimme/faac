/*
 * FAAC - Freeware Advanced Audio Coder
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

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "drc_analyzer.h"
#include "util.h"

#ifndef M_PI_DOUBLE
# define M_PI_DOUBLE 3.14159265358979323846
#endif

/* Sliding window parameter constants */
#define DRC_SUBBLOCK_DUR_SEC          0.100f  /* 100 ms sub-blocks */
#define DRC_LOUDNESS_BLOCKS           8       /* 800 ms loudness window (8 x 100 ms) */

/* Compression curve constants */
#define DRC_GATE_ABS_LUFS             -70.0f  /* Absolute gate threshold in LUFS */
#define DRC_TAPER_CEIL_LUFS           -50.0f  /* Taper ceiling threshold in LUFS */

/* Ballistics constants */
#define DRC_TAU_ATTACK                0.030f  /* 30 ms fast attack time constant */
#define DRC_MAX_CUT_STEP_DB           3.00f   /* Maximum cut increase per frame (slew rate limit) */

/*
 * Static curves, relative to the program reference level, as gain over
 * loudness x = L - ref: 2:1 boost below boost_start up to boost_max, a null
 * band, then 2:1 cut above cut_start that steepens to cut_slope past cut_knee.
 * Edges and release times follow the line-mode profiles of a reference
 * encoder, measured as a black box on noise ramps and steps, then shifted by
 * about -0.5 LU to line up with its gains on music and speech under this
 * analyzer's 800 ms meter.
 */
typedef struct DrcProfile {
    float boost_start;  /* LU below which boost begins (negative) */
    float boost_max;    /* dB */
    float cut_start;    /* LU above which cut begins */
    float cut_knee;     /* LU where the cut steepens; beyond the table range if never */
    float cut_slope;    /* dB per LU past the knee */
    float tau_release;  /* seconds */
} DrcProfile;

static const DrcProfile drc_profiles[] = {
    /* FAAC_DRC_FILM         */ { -4.0f,   6.0f,  1.0f,  10.5f, 0.90f, 2.5f  },
    /* FAAC_DRC_FILM_LIGHT   */ { -15.0f,  6.0f,  6.0f,  17.5f, 0.94f, 2.5f  },
    /* FAAC_DRC_MUSIC        */ { -4.0f,  12.0f,  1.0f,  10.5f, 0.90f, 7.5f  },
    /* FAAC_DRC_MUSIC_LIGHT  */ { -15.0f, 12.0f,  6.0f, 1000.0f, 0.5f, 2.5f  },
    /* FAAC_DRC_SPEECH       */ { -4.0f,  15.0f,  1.0f,  10.5f, 0.90f, 0.55f },
};

/* Gain in dB for loudness x LU relative to the program reference level. */
static float ProfileCurveDb(const DrcProfile *pr, float x)
{
    if (x < pr->boost_start) {
        float g = 0.5f * (pr->boost_start - x);
        return g < pr->boost_max ? g : pr->boost_max;
    }
    if (x <= pr->cut_start)
        return 0.0f;
    if (x <= pr->cut_knee)
        return -0.5f * (x - pr->cut_start);
    return -0.5f * (pr->cut_knee - pr->cut_start) - pr->cut_slope * (x - pr->cut_knee);
}

typedef struct BiquadState {
    float v1_1, v2_1; /* Pre-filter delay elements */
    float v1_2, v2_2; /* RLB filter delay elements */
    float weight;     /* BS.1770-4 channel weight (1.0 for main channels, 1.41 for surround, 0.0 for LFE) */
} BiquadState;

struct DrcAnalyzer {
    uint32_t sample_rate;
    uint32_t num_channels;
    float ref_level_lufs;
    int8_t ref_code;
    const DrcProfile *profile;

    /* BS.1770-4 K-weighting pre-filter biquad coefficients */
    float b0_1, b1_1, b2_1;
    float a1_1, a2_1;

    /* BS.1770-4 RLB high-pass filter biquad coefficients */
    float b0_2, b1_2, b2_2;
    float a1_2, a2_2;

    BiquadState ch_state[MAX_CHANNELS];

    /* Sub-block energy ring buffer for the loudness window */
    uint32_t subblock_size_samples;
    uint32_t subblock_sample_count;
    double current_subblock_sum;

    float subblock_energy[DRC_LOUDNESS_BLOCKS];
    uint32_t subblock_head;
    uint32_t subblock_count;

    /* Ballistics and 1-frame lookahead state */
    bool has_prev_gain;
    float prev_smooth_gain_db;
    float alpha_attack;
    float alpha_release;

    bool has_lookahead;
    DRCInfo lookahead_drc;

    /* Delay line from analyzed frame to emitted gain: hist[0] is the newest. */
    bool hist_primed;
    DRCInfo hist[2];
};

/*
 * Compute ITU-R BS.1770-4 K-weighting pre-filter biquad coefficients
 * for arbitrary sample rates via bilinear transform.
 */
static void ComputePreFilterCoeffs(double fs, float *b0, float *b1, float *b2, float *a1, float *a2)
{
    double f0 = 1681.974450955533;
    double Q  = 0.7071752369554196;

    double K = tan(M_PI_DOUBLE * f0 / fs);
    /* pow(10, G/20) and its square root for the fixed shelf gain. Keeping
     * them as literals avoids carrying the double-precision pow/sqrt path
     * in every build; the values are the exact round-to-nearest results. */
    double V0 = 1.5848647011308556;
    double sv = 1.2589140960092773;

    double d = 1.0 + (1.0 / Q) * K + K * K;
    double b0_d = (V0 + sv * (1.0 / Q) * K + K * K) / d;
    double b1_d = (2.0 * (K * K - V0)) / d;
    double b2_d = (V0 - sv * (1.0 / Q) * K + K * K) / d;
    double a1_d = (2.0 * (K * K - 1.0)) / d;
    double a2_d = (1.0 - (1.0 / Q) * K + K * K) / d;

    *b0 = (float)b0_d; *b1 = (float)b1_d; *b2 = (float)b2_d;
    *a1 = (float)a1_d; *a2 = (float)a2_d;
}

/*
 * Compute ITU-R BS.1770-4 RLB high-pass filter biquad coefficients
 * for arbitrary sample rates via bilinear transform.
 */
static void ComputeRLBCoeffs(double fs, float *b0, float *b1, float *b2, float *a1, float *a2)
{
    double f0 = 38.13547087602444;
    double Q  = 0.5003270373238773;

    double K = tan(M_PI_DOUBLE * f0 / fs);
    double d = 1.0 + (1.0 / Q) * K + K * K;

    double b0_d = 1.0 / d;
    double b1_d = -2.0 / d;
    double b2_d = 1.0 / d;
    double a1_d = (2.0 * (K * K - 1.0)) / d;
    double a2_d = (1.0 - (1.0 / Q) * K + K * K) / d;

    *b0 = (float)b0_d; *b1 = (float)b1_d; *b2 = (float)b2_d;
    *a1 = (float)a1_d; *a2 = (float)a2_d;
}

DrcAnalyzer *DrcAnalyzerInit(uint32_t sample_rate, uint32_t num_channels,
                             const bool *is_lfe_channel, float ref_level_lufs,
                             uint32_t profile, uint32_t samples_per_ch)
{
    if (sample_rate == 0 || num_channels == 0 || num_channels > MAX_CHANNELS
        || samples_per_ch == 0
        || profile < 1 || profile > sizeof(drc_profiles) / sizeof(drc_profiles[0]))
        return NULL;

    DrcAnalyzer *da = (DrcAnalyzer *)AllocMemory(sizeof(DrcAnalyzer));
    if (!da)
        return NULL;
    memset(da, 0, sizeof(DrcAnalyzer));

    da->sample_rate = sample_rate;
    da->num_channels = num_channels;
    da->ref_level_lufs = ref_level_lufs;
    int ref_code = (int)(fabsf(ref_level_lufs) / 0.25f + 0.5f);
    if (ref_code > 127)
        ref_code = 127;
    da->ref_code = (int8_t)ref_code;
    da->profile = &drc_profiles[profile - 1];

    float frame_dur = (float)samples_per_ch / (float)sample_rate;
    da->alpha_attack = 1.0f - expf(-frame_dur / DRC_TAU_ATTACK);
    da->alpha_release = 1.0f - expf(-frame_dur / da->profile->tau_release);

    ComputePreFilterCoeffs((double)sample_rate, &da->b0_1, &da->b1_1, &da->b2_1, &da->a1_1, &da->a2_1);
    ComputeRLBCoeffs((double)sample_rate, &da->b0_2, &da->b1_2, &da->b2_2, &da->a1_2, &da->a2_2);

    /* Assign BS.1770-4 channel weights */
    for (uint32_t ch = 0; ch < num_channels; ch++) {
        if (is_lfe_channel && is_lfe_channel[ch]) {
            da->ch_state[ch].weight = 0.0f; /* LFE is excluded from ITU-R BS.1770 loudness measurement */
        } else if (num_channels >= 5 && (ch == 3 || ch == 4)) {
            /* The FIFO is in AAC order (C L R Ls Rs [LFE]), so surrounds sit
             * at 3 and 4. They get the BS.1770 ~1.5 dB (+1.41 linear) boost. */
            da->ch_state[ch].weight = 1.41f;
        } else {
            da->ch_state[ch].weight = 1.0f;  /* Main front/side/center channels */
        }
    }

    da->subblock_size_samples = (uint32_t)((float)sample_rate * DRC_SUBBLOCK_DUR_SEC + 0.5f);
    if (da->subblock_size_samples == 0)
        da->subblock_size_samples = 1;

    return da;
}

static void AnalyzeFrame(DrcAnalyzer *da, float * const *input_pcm,
                         uint32_t samples_per_ch, DRCInfo *out_drc)
{

    float max_peak_lin = 0.0f;
    double frame_sum_sq = 0.0;
    const float pcm_scale = 1.0f / 32768.0f;

    /* Hoist biquad coefficients to local variables */
    const float b0_1 = da->b0_1, b1_1 = da->b1_1, b2_1 = da->b2_1, a1_1 = da->a1_1, a2_1 = da->a2_1;
    const float b0_2 = da->b0_2, b1_2 = da->b1_2, b2_2 = da->b2_2, a1_2 = da->a1_2, a2_2 = da->a2_2;

    /* Process channel-by-channel for cache locality and SIMD vectorization */
    for (uint32_t ch = 0; ch < da->num_channels; ch++) {
        const float *pcm = input_pcm[ch];
        const float weight = da->ch_state[ch].weight;

        /* Find peak linear magnitude */
        for (uint32_t i = 0; i < samples_per_ch; i++) {
            float abs_x = fabsf(pcm[i] * pcm_scale);
            if (abs_x > max_peak_lin)
                max_peak_lin = abs_x;
        }

        if (weight <= 0.0f)
            continue;

        float v1_1 = da->ch_state[ch].v1_1, v2_1 = da->ch_state[ch].v2_1;
        float v1_2 = da->ch_state[ch].v1_2, v2_2 = da->ch_state[ch].v2_2;
        double ch_sum_sq = 0.0;

        for (uint32_t i = 0; i < samples_per_ch; i++) {
            float x = pcm[i] * pcm_scale;

            /* Stage 1: High-shelf pre-filter (Direct Form II Transposed) */
            float y1 = b0_1 * x + v1_1;
            v1_1 = b1_1 * x - a1_1 * y1 + v2_1;
            v2_1 = b2_1 * x - a2_1 * y1;

            /* Stage 2: RLB high-pass filter */
            float y2 = b0_2 * y1 + v1_2;
            v1_2 = b1_2 * y1 - a1_2 * y2 + v2_2;
            v2_2 = b2_2 * y1 - a2_2 * y2;

            ch_sum_sq += (double)y2 * (double)y2;
        }

        da->ch_state[ch].v1_1 = v1_1; da->ch_state[ch].v2_1 = v2_1;
        da->ch_state[ch].v1_2 = v1_2; da->ch_state[ch].v2_2 = v2_2;

        frame_sum_sq += (double)weight * ch_sum_sq;
    }

    /* Accumulate sub-block energy ring buffer */
    da->current_subblock_sum += frame_sum_sq;
    da->subblock_sample_count += samples_per_ch;

    while (da->subblock_sample_count >= da->subblock_size_samples) {
        /* A frame can overshoot the sub-block boundary, so the sum covers
         * sample_count samples; normalise by that and carry the remainder's
         * energy forward with its samples. */
        float mean_sq = (float)(da->current_subblock_sum / (double)da->subblock_sample_count);
        da->subblock_energy[da->subblock_head] = mean_sq;
        da->subblock_head = (da->subblock_head + 1) % DRC_LOUDNESS_BLOCKS;
        if (da->subblock_count < DRC_LOUDNESS_BLOCKS)
            da->subblock_count++;

        da->current_subblock_sum -= (double)mean_sq * (double)da->subblock_size_samples;
        if (da->current_subblock_sum < 0.0)
            da->current_subblock_sum = 0.0;
        da->subblock_sample_count -= da->subblock_size_samples;
    }

    /*
     * Loudness over the last 800 ms drives the curve. The sub-block still
     * filling counts toward the mean, so an onset shows in the frame that
     * carries it instead of up to 100 ms later. A louder per-frame value used
     * as an attack override made cuts on dynamic music 1 to 4 dB deeper than
     * the reference profiles, and a 3 s mean took over a second to follow a
     * loud step.
     */
    double energy = da->current_subblock_sum;
    double count = (double)da->subblock_sample_count;
    uint32_t used = da->subblock_count < DRC_LOUDNESS_BLOCKS ? da->subblock_count : DRC_LOUDNESS_BLOCKS;
    for (uint32_t i = 0; i < used; i++) {
        uint32_t idx = (da->subblock_head + DRC_LOUDNESS_BLOCKS - 1 - i) % DRC_LOUDNESS_BLOCKS;
        energy += (double)da->subblock_energy[idx] * (double)da->subblock_size_samples;
    }
    count += (double)used * (double)da->subblock_size_samples;
    float z_short = (float)(energy / count);

    float L_eff = (z_short > 1e-12f) ? (-0.691f + 10.0f * log10f(z_short)) : -100.0f;

    /*
     * Profile curve, with an absolute gate: below -70 LUFS the gain is 0, and
     * boost ramps in linearly up to the profile's maximum at -50 LUFS so noise
     * floors and fade tails are not lifted.
     */
    float gain_raw_db = 0.0f;
    if (L_eff >= DRC_GATE_ABS_LUFS) {
        gain_raw_db = ProfileCurveDb(da->profile, L_eff - da->ref_level_lufs);
        if (gain_raw_db > 0.0f && L_eff < DRC_TAPER_CEIL_LUFS) {
            float gain_taper = da->profile->boost_max
                * ((L_eff - DRC_GATE_ABS_LUFS) / (DRC_TAPER_CEIL_LUFS - DRC_GATE_ABS_LUFS));
            if (gain_taper < gain_raw_db)
                gain_raw_db = gain_taper;
        }
    }

    /*
     * Ballistics & Slew-limiting:
     * - Fast attack (tau = 30 ms) for cuts, capped by DRC_MAX_CUT_STEP_DB (3.0 dB / frame).
     * - Release (profile tau) for releases and boosts.
     */
    float gain_smooth_db = da->prev_smooth_gain_db;

    if (!da->has_prev_gain) {
        gain_smooth_db = gain_raw_db;
        da->has_prev_gain = true;
    } else if (gain_raw_db < da->prev_smooth_gain_db) {
        /* Gain cut (Attack) */
        gain_smooth_db = da->prev_smooth_gain_db + da->alpha_attack * (gain_raw_db - da->prev_smooth_gain_db);
        if (gain_smooth_db < da->prev_smooth_gain_db - DRC_MAX_CUT_STEP_DB)
            gain_smooth_db = da->prev_smooth_gain_db - DRC_MAX_CUT_STEP_DB;
    } else {
        /* Gain boost / Cut release */
        gain_smooth_db = da->prev_smooth_gain_db + da->alpha_release * (gain_raw_db - da->prev_smooth_gain_db);
    }

    /*
     * Peak Headroom Clip Protection:
     * Instantaneous limit preventing boost gain from exceeding headroom to 0 dBFS.
     */
    if (gain_smooth_db > 0.0f) {
        float peak_dbfs = 20.0f * log10f((max_peak_lin > 1e-9f) ? max_peak_lin : 1e-9f);
        float headroom_db = (peak_dbfs < 0.0f) ? -peak_dbfs : 0.0f;
        if (gain_smooth_db > headroom_db)
            gain_smooth_db = headroom_db;
    }

    /* Quantize to the existing 0.25 dB grid and keep the wire code. */
    float gain_quant = roundf(gain_smooth_db / 0.25f);
    if (gain_quant > 127.0f)
        gain_quant = 127.0f;
    if (gain_quant < -127.0f)
        gain_quant = -127.0f;

    DRCInfo curr_drc;
    curr_drc.enabled = true;
    curr_drc.gain_code = (int8_t)(int)gain_quant;
    curr_drc.ref_code = da->ref_code;

    /*
     * 1-Frame Lookahead Output:
     * Lookahead allows incoming transient cuts at frame M to take effect at frame M-1 (K_step - 1).
     */
    if (!da->has_lookahead) {
        da->lookahead_drc = curr_drc;
        da->has_lookahead = true;
        *out_drc = curr_drc;
    } else {
        /* If incoming frame M requires a cut deeper than lookahead frame, advance cut to lookahead frame */
        if (curr_drc.gain_code < da->lookahead_drc.gain_code) {
            da->lookahead_drc.gain_code = curr_drc.gain_code;
        }
        *out_drc = da->lookahead_drc;
        da->lookahead_drc = curr_drc;
    }

    /* Carry the unquantized gain: feeding back the 0.25 dB-rounded output
     * freezes the 1 s release, whose per-frame step is far below the grid. */
    da->prev_smooth_gain_db = gain_smooth_db;
}

void DrcAnalyzerProcessFrame(DrcAnalyzer *da, float * const *input_pcm,
                             uint32_t samples_per_ch, DRCInfo *out_drc)
{
    if (!da || !out_drc)
        return;

    /* The analyzer sees the newest input frame, but the access unit being
     * written carries audio that entered the encoder one frame earlier. Delay
     * the gain so the FIL describes the audio it travels with. A call without
     * input (flush) keeps the pipeline ticking on the last gain. */
    DRCInfo cur = da->hist[0];
    if (input_pcm && samples_per_ch > 0)
        AnalyzeFrame(da, input_pcm, samples_per_ch, &cur);
    else if (!da->hist_primed)
        return;

    if (!da->hist_primed) {
        da->hist[1] = cur;
        da->hist[0] = cur;
        da->hist_primed = true;
    } else {
        da->hist[1] = da->hist[0];
        da->hist[0] = cur;
    }
    *out_drc = da->hist[1];
}
