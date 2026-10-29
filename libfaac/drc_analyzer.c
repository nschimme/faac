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
#define DRC_NUM_SUBBLOCKS_3S          30      /* 3.0 s short-term window (30 x 100 ms) */
#define DRC_NUM_SUBBLOCKS_400MS       4       /* 400 ms momentary window (4 x 100 ms) */

/* Compression curve constants */
#define DRC_GATE_ABS_LUFS             -70.0f  /* Absolute gate threshold in LUFS */
#define DRC_TAPER_CEIL_LUFS           -50.0f  /* Taper ceiling threshold in LUFS */
#define DRC_MAX_BOOST_DB              6.0f    /* Maximum expansion boost in dB */
#define DRC_MAX_CUT_DB                -15.0f  /* Maximum compression cut in dB */
#define DRC_MOMENTARY_OVERRIDE_LU     3.0f    /* Transient attack override threshold */

/* Ballistics constants */
#define DRC_TAU_ATTACK                0.030f  /* 30 ms fast attack time constant */
#define DRC_TAU_RELEASE               1.000f  /* 1000 ms slow release time constant */
#define DRC_MAX_CUT_STEP_DB           3.00f   /* Maximum cut increase per frame (slew rate limit) */

typedef struct BiquadState {
    float v1_1, v2_1; /* Pre-filter delay elements */
    float v1_2, v2_2; /* RLB filter delay elements */
    float weight;     /* BS.1770-4 channel weight (1.0 for main channels, 1.41 for surround, 0.0 for LFE) */
} BiquadState;

struct DrcAnalyzer {
    uint32_t sample_rate;
    uint32_t num_channels;
    float ref_level_lufs;

    /* BS.1770-4 K-weighting pre-filter biquad coefficients */
    float b0_1, b1_1, b2_1;
    float a1_1, a2_1;

    /* BS.1770-4 RLB high-pass filter biquad coefficients */
    float b0_2, b1_2, b2_2;
    float a1_2, a2_2;

    BiquadState ch_state[MAX_CHANNELS];

    /* Sub-block energy ring buffer for 3s / 400ms sliding windows */
    uint32_t subblock_size_samples;
    uint32_t subblock_sample_count;
    double current_subblock_sum;

    float subblock_energy[DRC_NUM_SUBBLOCKS_3S];
    uint32_t subblock_head;
    uint32_t subblock_count;

    /* Ballistics and 1-frame lookahead state */
    bool has_prev_gain;
    float prev_smooth_gain_db;

    bool has_lookahead;
    DRCInfo lookahead_drc;
};

/*
 * Compute ITU-R BS.1770-4 K-weighting pre-filter biquad coefficients
 * for arbitrary sample rates via bilinear transform.
 */
static void ComputePreFilterCoeffs(double fs, float *b0, float *b1, float *b2, float *a1, float *a2)
{
    double f0 = 1681.974450955533;
    double G  = 3.999843853973347;
    double Q  = 0.7071752369554196;

    double K = tan(M_PI_DOUBLE * f0 / fs);
    double V0 = pow(10.0, G / 20.0);

    double d = 1.0 + (1.0 / Q) * K + K * K;
    double b0_d = (V0 + sqrt(V0) * (1.0 / Q) * K + K * K) / d;
    double b1_d = (2.0 * (K * K - V0)) / d;
    double b2_d = (V0 - sqrt(V0) * (1.0 / Q) * K + K * K) / d;
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
                             const bool *is_lfe_channel, float ref_level_lufs)
{
    if (sample_rate == 0 || num_channels == 0 || num_channels > MAX_CHANNELS)
        return NULL;

    DrcAnalyzer *da = (DrcAnalyzer *)AllocMemory(sizeof(DrcAnalyzer));
    if (!da)
        return NULL;
    memset(da, 0, sizeof(DrcAnalyzer));

    da->sample_rate = sample_rate;
    da->num_channels = num_channels;
    da->ref_level_lufs = ref_level_lufs;

    ComputePreFilterCoeffs((double)sample_rate, &da->b0_1, &da->b1_1, &da->b2_1, &da->a1_1, &da->a2_1);
    ComputeRLBCoeffs((double)sample_rate, &da->b0_2, &da->b1_2, &da->b2_2, &da->a1_2, &da->a2_2);

    /* Assign BS.1770-4 channel weights */
    for (uint32_t ch = 0; ch < num_channels; ch++) {
        if (is_lfe_channel && is_lfe_channel[ch]) {
            da->ch_state[ch].weight = 0.0f; /* LFE is excluded from ITU-R BS.1770 loudness measurement */
        } else if (num_channels >= 5 && (ch == 4 || ch == 5)) {
            da->ch_state[ch].weight = 1.41f; /* Surround channels get ~1.5 dB (+1.41 linear) boost */
        } else {
            da->ch_state[ch].weight = 1.0f;  /* Main front/side/center channels */
        }
    }

    da->subblock_size_samples = (uint32_t)((float)sample_rate * DRC_SUBBLOCK_DUR_SEC + 0.5f);
    if (da->subblock_size_samples == 0)
        da->subblock_size_samples = 1;

    return da;
}

void DrcAnalyzerProcessFrame(DrcAnalyzer *da, float * const *input_pcm,
                             uint32_t samples_per_ch, DRCInfo *out_drc)
{
    if (!da || !input_pcm || samples_per_ch == 0 || !out_drc)
        return;

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
        float mean_sq = (float)(da->current_subblock_sum / (double)da->subblock_size_samples);
        da->subblock_energy[da->subblock_head] = mean_sq;
        da->subblock_head = (da->subblock_head + 1) % DRC_NUM_SUBBLOCKS_3S;
        if (da->subblock_count < DRC_NUM_SUBBLOCKS_3S)
            da->subblock_count++;

        da->current_subblock_sum -= (double)mean_sq * (double)da->subblock_size_samples;
        if (da->current_subblock_sum < 0.0)
            da->current_subblock_sum = 0.0;
        da->subblock_sample_count -= da->subblock_size_samples;
    }

    /*
     * Compute short-term (3 s), momentary (400 ms), and per-frame loudness.
     */
    float z_short = 0.0f;
    float z_momentary = 0.0f;
    float z_frame = (float)(frame_sum_sq / (double)samples_per_ch);

    if (da->subblock_count > 0) {
        float sum_3s = 0.0f;
        for (uint32_t i = 0; i < da->subblock_count; i++)
            sum_3s += da->subblock_energy[i];
        z_short = sum_3s / (float)da->subblock_count;

        uint32_t m_count = (da->subblock_count < DRC_NUM_SUBBLOCKS_400MS)
                         ? da->subblock_count : DRC_NUM_SUBBLOCKS_400MS;
        float sum_400ms = 0.0f;
        for (uint32_t i = 0; i < m_count; i++) {
            uint32_t idx = (da->subblock_head + DRC_NUM_SUBBLOCKS_3S - 1 - i) % DRC_NUM_SUBBLOCKS_3S;
            sum_400ms += da->subblock_energy[idx];
        }
        z_momentary = sum_400ms / (float)m_count;
    } else {
        z_short = z_momentary = z_frame;
    }

    float L_short = (z_short > 1e-12f) ? (-0.691f + 10.0f * log10f(z_short)) : -100.0f;
    float L_momentary = (z_momentary > 1e-12f) ? (-0.691f + 10.0f * log10f(z_momentary)) : -100.0f;
    float L_frame = (z_frame > 1e-12f) ? (-0.691f + 10.0f * log10f(z_frame)) : -100.0f;

    /*
     * Effective loudness selection: short-term baseline, overridden by per-frame
     * or momentary loudness when a transient attack exceeds short-term by > 3.0 LU.
     */
    float L_eff = L_short;
    if (L_frame > L_short + DRC_MOMENTARY_OVERRIDE_LU) {
        L_eff = L_frame;
    } else if (L_momentary > L_short + DRC_MOMENTARY_OVERRIDE_LU) {
        L_eff = L_momentary;
    }

    /*
     * Compression curve and gating:
     * - Absolute gate: below -70.0 LUFS, gain is 0.0 dB (no boost).
     * - Linear boost taper: between -70.0 LUFS and -50.0 LUFS, boost gain is linearly
     *   tapered from 0.0 dB at -70.0 LUFS to +6.0 dB at -50.0 LUFS.
     * - Full boost: between -50.0 LUFS and reference level, 2:1 boost up to +6.0 dB.
     * - Cut: above reference level, 2:1 compression up to -15.0 dB max cut.
     */
    float g_raw_db = 0.0f;
    if (L_eff < DRC_GATE_ABS_LUFS) {
        g_raw_db = 0.0f;
    } else if (L_eff < DRC_TAPER_CEIL_LUFS) {
        float g_comp = -0.5f * (L_eff - da->ref_level_lufs);
        float g_taper = 6.0f * ((L_eff - DRC_GATE_ABS_LUFS) / (DRC_TAPER_CEIL_LUFS - DRC_GATE_ABS_LUFS));
        g_raw_db = (g_taper < g_comp) ? g_taper : g_comp;
    } else if (L_eff < da->ref_level_lufs) {
        float g_boost = -0.5f * (L_eff - da->ref_level_lufs);
        g_raw_db = (g_boost < DRC_MAX_BOOST_DB) ? g_boost : DRC_MAX_BOOST_DB;
    } else {
        float g_cut = -0.5f * (L_eff - da->ref_level_lufs);
        g_raw_db = (g_cut > DRC_MAX_CUT_DB) ? g_cut : DRC_MAX_CUT_DB;
    }

    /*
     * Ballistics & Slew-limiting:
     * - Fast attack (tau = 30 ms) for cuts, capped by DRC_MAX_CUT_STEP_DB (3.0 dB / frame).
     * - Slow release (tau = 1000 ms) for releases and boosts.
     */
    float frame_dur = (float)samples_per_ch / (float)da->sample_rate;
    float g_smooth_db = da->prev_smooth_gain_db;

    if (!da->has_prev_gain) {
        g_smooth_db = g_raw_db;
        da->has_prev_gain = true;
    } else if (g_raw_db < da->prev_smooth_gain_db) {
        /* Gain cut (Attack) */
        float alpha_attack = 1.0f - expf(-frame_dur / DRC_TAU_ATTACK);
        g_smooth_db = da->prev_smooth_gain_db + alpha_attack * (g_raw_db - da->prev_smooth_gain_db);
        if (g_smooth_db < da->prev_smooth_gain_db - DRC_MAX_CUT_STEP_DB)
            g_smooth_db = da->prev_smooth_gain_db - DRC_MAX_CUT_STEP_DB;
    } else {
        /* Gain boost / Cut release */
        float alpha_release = 1.0f - expf(-frame_dur / DRC_TAU_RELEASE);
        g_smooth_db = da->prev_smooth_gain_db + alpha_release * (g_raw_db - da->prev_smooth_gain_db);
    }

    /*
     * Peak Headroom Clip Protection:
     * Instantaneous limit preventing boost gain from exceeding headroom to 0 dBFS.
     */
    if (g_smooth_db > 0.0f) {
        float peak_dbfs = 20.0f * log10f((max_peak_lin > 1e-9f) ? max_peak_lin : 1e-9f);
        float headroom_db = (peak_dbfs < 0.0f) ? -peak_dbfs : 0.0f;
        if (g_smooth_db > headroom_db)
            g_smooth_db = headroom_db;
    }

    /* Quantize to existing 0.25 dB grid */
    float g_quant_db = roundf(g_smooth_db / 0.25f) * 0.25f;
    if (g_quant_db > 31.75f)
        g_quant_db = 31.75f;
    if (g_quant_db < -31.75f)
        g_quant_db = -31.75f;

    DRCInfo curr_drc;
    curr_drc.enabled = true;
    curr_drc.gain_db = g_quant_db;
    curr_drc.prog_ref_level_present = true;
    curr_drc.prog_ref_level_db = da->ref_level_lufs;

    /*
     * 1-Frame Lookahead Output:
     * Lookahead allows incoming transient cuts at frame M to take effect at frame M-1 (K_step - 1).
     */
    if (!da->has_lookahead) {
        da->lookahead_drc = curr_drc;
        da->has_lookahead = true;
        *out_drc = curr_drc;
        da->prev_smooth_gain_db = g_smooth_db;
    } else {
        /* If incoming frame M requires a cut deeper than lookahead frame, advance cut to lookahead frame */
        if (curr_drc.gain_db < da->lookahead_drc.gain_db) {
            da->lookahead_drc.gain_db = curr_drc.gain_db;
        }
        *out_drc = da->lookahead_drc;
        da->prev_smooth_gain_db = da->lookahead_drc.gain_db;
        da->lookahead_drc = curr_drc;
    }
}

void DrcAnalyzerEnd(DrcAnalyzer *da)
{
    if (!da)
        return;

    FreeMemory(da);
}
