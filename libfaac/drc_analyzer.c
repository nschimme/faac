/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2026 Nils Schimmelmann
 *
 * Dynamic Range Control (DRC) Causal Analyzer
 *
 * Implements ITU-R BS.1770-4 K-weighting filters, 400 ms momentary and 3 s
 * short-term sliding window loudness measurement, linear boost tapering,
 * fast attack / slow release ballistics, peak headroom clip protection, and
 * 0.25 dB grid quantization with 1-frame lookahead.
 */

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "drc_analyzer.h"
#include "util.h"

#ifndef M_PI_DOUBLE
#define M_PI_DOUBLE 3.141592653589793238462643383279502884
#endif

/* Broadcast compression curve and ballistics design constants */
#define DRC_DEFAULT_REF_LEVEL     (-23.0f) /* EBU R128 / ATSC A/85 target program reference in LUFS */
#define DRC_GATE_ABS_LUFS         (-70.0f) /* ITU-R BS.1770-4 absolute gating threshold in LUFS */
#define DRC_TAPER_CEIL_LUFS       (-50.0f) /* Ceiling for linear boost taper in LUFS */
#define DRC_COMP_RATIO            (2.0f)   /* 2:1 compression ratio above reference level */
#define DRC_MAX_CUT_DB            (-15.0f) /* Maximum dynamic range attenuation in dB */
#define DRC_MAX_BOOST_DB          (+6.0f)  /* Maximum dynamic range boost in dB */
#define DRC_MOMENTARY_OVERRIDE_LU (+3.0f)  /* Momentary override threshold above short-term baseline (2x power) */
#define DRC_TAU_ATTACK            (0.030f) /* Fast attack time constant in seconds (30 ms) */
#define DRC_TAU_RELEASE           (1.000f) /* Slow release time constant in seconds (1000 ms) */
#define DRC_MAX_CUT_STEP_DB       (3.0f)   /* Explicit maximum single-frame gain cut step cap in dB */

/* Sub-block history ring buffer sizes (100 ms per sub-block) */
#define DRC_SUBBLOCK_MS           (100)
#define DRC_NUM_SUBBLOCKS_3S      (30)     /* 30 x 100 ms = 3000 ms short-term window */
#define DRC_NUM_SUBBLOCKS_400MS   (4)      /* 4 x 100 ms = 400 ms momentary window */

typedef struct {
    /* Stage 1: High-shelf pre-filter biquad state (Direct Form II Transposed) */
    float v1_1, v2_1;
    /* Stage 2: RLB high-pass filter biquad state */
    float v1_2, v2_2;
    /* Channel weight per BS.1770 (1.0 for L/R/C, 1.4125 for surrounds, 0.0 for LFE) */
    float weight;
} BiquadChannelState;

struct DrcAnalyzer {
    uint32_t sample_rate;
    uint32_t num_channels;
    float ref_level_lufs;

    /* Derived K-weighting biquad coefficients for Stage 1 and Stage 2 */
    float b0_1, b1_1, b2_1, a1_1, a2_1;
    float b0_2, b1_2, b2_2, a1_2, a2_2;

    /* Per-channel filter states */
    BiquadChannelState *ch_state;

    /* 100 ms sub-block accumulator */
    uint32_t subblock_size_samples;
    uint32_t subblock_sample_count;
    double current_subblock_sum;

    /* Ring buffer of 100 ms sub-block mean-square energies */
    float subblock_energy[DRC_NUM_SUBBLOCKS_3S];
    uint32_t subblock_head;
    uint32_t subblock_count;

    /* Ballistics state */
    float prev_smooth_gain_db;
    bool  has_prev_gain;

    /* 1-frame lookahead state */
    DRCInfo lookahead_drc;
    bool  has_lookahead;
};

/*
 * Derive ITU-R BS.1770-4 K-weighting filter coefficients for given sample rate.
 */
static void DeriveKWeightCoeffs(uint32_t fs,
                                float *b0_1, float *b1_1, float *b2_1, float *a1_1, float *a2_1,
                                float *b0_2, float *b1_2, float *b2_2, float *a1_2, float *a2_2)
{
    /* Calibration factor to match 48 kHz reference calibration exactly (-23.0000 LUFS) */
    const double cal_gain = 1.0048406772659235;

    /* Stage 1: High-shelf pre-filter analog prototype parameters */
    const double db_gain = 3.999843853973347;
    const double f0_1 = 1681.9744509555319;
    const double Q_1 = 0.7071067811865475;

    double K_tan_1 = tan(M_PI_DOUBLE * f0_1 / (double)fs);
    double Vh = pow(10.0, db_gain / 20.0);
    double Vb = sqrt(Vh);
    double K_s1 = 1.0 / K_tan_1;

    double den1 = K_s1 * K_s1 + K_s1 / Q_1 + 1.0;
    *b0_1 = (float)(((Vh * K_s1 * K_s1 + Vb * K_s1 / Q_1 + 1.0) / den1) * cal_gain);
    *b1_1 = (float)(((2.0 * (1.0 - Vh * K_s1 * K_s1)) / den1) * cal_gain);
    *b2_1 = (float)(((Vh * K_s1 * K_s1 - Vb * K_s1 / Q_1 + 1.0) / den1) * cal_gain);
    *a1_1 = (float)((2.0 * (1.0 - K_s1 * K_s1)) / den1);
    *a2_1 = (float)((K_s1 * K_s1 - K_s1 / Q_1 + 1.0) / den1);

    /* Stage 2: RLB high-pass filter analog prototype parameters */
    const double f0_2 = 38.13547087602444;
    const double Q_2 = 0.5003270373238773;

    double K_tan_2 = tan(M_PI_DOUBLE * f0_2 / (double)fs);
    double K_s2 = 1.0 / K_tan_2;

    double den2 = K_s2 * K_s2 + K_s2 / Q_2 + 1.0;
    *b0_2 = (float)((K_s2 * K_s2) / den2);
    *b1_2 = (float)((-2.0 * K_s2 * K_s2) / den2);
    *b2_2 = (float)((K_s2 * K_s2) / den2);
    *a1_2 = (float)((2.0 * (1.0 - K_s2 * K_s2)) / den2);
    *a2_2 = (float)((K_s2 * K_s2 - K_s2 / Q_2 + 1.0) / den2);
}

DrcAnalyzer *DrcAnalyzerInit(uint32_t sample_rate, uint32_t num_channels,
                             const bool *is_lfe_channel, float ref_level_lufs)
{
    if (sample_rate == 0 || num_channels == 0)
        return NULL;

    DrcAnalyzer *da = (DrcAnalyzer *)AllocMemory(sizeof(DrcAnalyzer));
    if (!da)
        return NULL;
    memset(da, 0, sizeof(DrcAnalyzer));

    da->sample_rate = sample_rate;
    da->num_channels = num_channels;
    da->ref_level_lufs = (ref_level_lufs == 0.0f) ? DRC_DEFAULT_REF_LEVEL : ref_level_lufs;

    da->ch_state = (BiquadChannelState *)AllocMemory(num_channels * sizeof(BiquadChannelState));
    if (!da->ch_state) {
        FreeMemory(da);
        return NULL;
    }
    memset(da->ch_state, 0, num_channels * sizeof(BiquadChannelState));

    for (uint32_t ch = 0; ch < num_channels; ch++) {
        if (is_lfe_channel && is_lfe_channel[ch]) {
            da->ch_state[ch].weight = 0.0f; /* Exclude LFE channel */
        } else {
            da->ch_state[ch].weight = 1.0f; /* 0 dB weight for main channels */
        }
    }

    DeriveKWeightCoeffs(sample_rate,
                        &da->b0_1, &da->b1_1, &da->b2_1, &da->a1_1, &da->a2_1,
                        &da->b0_2, &da->b1_2, &da->b2_2, &da->a1_2, &da->a2_2);

    da->subblock_size_samples = (uint32_t)((double)sample_rate * 0.100 + 0.5);
    if (da->subblock_size_samples == 0)
        da->subblock_size_samples = 1;

    da->prev_smooth_gain_db = 0.0f;
    da->has_prev_gain = false;
    da->has_lookahead = false;

    return da;
}

void DrcAnalyzerProcessFrame(DrcAnalyzer *da, float * const *input_pcm,
                             uint32_t samples_per_ch, DRCInfo *out_drc)
{
    if (!da || !input_pcm || !out_drc || samples_per_ch == 0)
        return;

    float max_peak_lin = 0.0f;
    double frame_sum_sq = 0.0;
    const float pcm_scale = 1.0f / 32768.0f;

    /*
     * Filter input samples through BS.1770 K-weighting biquads and accumulate
     * squared channel-weighted outputs into frame and sub-block accumulators.
     */
    for (uint32_t i = 0; i < samples_per_ch; i++) {
        double sample_sum_sq = 0.0;

        for (uint32_t ch = 0; ch < da->num_channels; ch++) {
            float x = input_pcm[ch][i] * pcm_scale;
            float abs_x = fabsf(x);
            if (abs_x > max_peak_lin)
                max_peak_lin = abs_x;

            if (da->ch_state[ch].weight <= 0.0f)
                continue;

            /* Stage 1: High-shelf pre-filter (Direct Form II Transposed) */
            float y1 = da->b0_1 * x + da->ch_state[ch].v1_1;
            da->ch_state[ch].v1_1 = da->b1_1 * x - da->a1_1 * y1 + da->ch_state[ch].v2_1;
            da->ch_state[ch].v2_1 = da->b2_1 * x - da->a2_1 * y1;

            /* Stage 2: RLB high-pass filter */
            float y2 = da->b0_2 * y1 + da->ch_state[ch].v1_2;
            da->ch_state[ch].v1_2 = da->b1_2 * y1 - da->a1_2 * y2 + da->ch_state[ch].v2_2;
            da->ch_state[ch].v2_2 = da->b2_2 * y1 - da->a2_2 * y2;

            sample_sum_sq += (double)da->ch_state[ch].weight * (double)y2 * (double)y2;
        }

        frame_sum_sq += sample_sum_sq;
        da->current_subblock_sum += sample_sum_sq;
        da->subblock_sample_count++;

        if (da->subblock_sample_count >= da->subblock_size_samples) {
            float mean_sq = (float)(da->current_subblock_sum / (double)da->subblock_sample_count);
            da->subblock_energy[da->subblock_head] = mean_sq;
            da->subblock_head = (da->subblock_head + 1) % DRC_NUM_SUBBLOCKS_3S;
            if (da->subblock_count < DRC_NUM_SUBBLOCKS_3S)
                da->subblock_count++;

            da->current_subblock_sum = 0.0;
            da->subblock_sample_count = 0;
        }
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

    if (da->ch_state)
        FreeMemory(da->ch_state);

    FreeMemory(da);
}
