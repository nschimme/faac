/*
 * FAAC - Freeware Advanced Audio Coder
 * ML-Optimized Audio Shaping Model Weights & Decision Policy
 * Generated automatically by scripts/ml_shaping/train_shaping_model.py
 */

#ifndef ML_SHAPING_MODEL_H
#define ML_SHAPING_MODEL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { MASKING_TILT_KNOTS = 8, MASKING_PROFILE_DEFAULT = 0, MASKING_PROFILE_TUNED = 1 };

typedef struct
{
    float frequency_khz;
    float gain;
} MaskingTiltKnot;

typedef struct
{
    float avg_weight;
    float loud_exponent;
    float slope_scale;
    float peak_floor;
} MaskingProfile;

typedef struct
{
    float silence_rms;
    float peak_weight;
    float avg_floor;
    float quiet_band;
    float sf_smooth;
    float rich_slope;
    unsigned tune_min_rate;
    unsigned nudge_min_rate;
    unsigned rich_min_rate;
    unsigned slope_rate_threshold;
    float slope_reference_rate;
    float tilt_nyquist_khz;
    MaskingProfile profiles[2];
    MaskingTiltKnot tilt[MASKING_TILT_KNOTS];
} MaskingModel;

/* ML-Optimized Masking Model parameters */
static const MaskingModel g_ml_masking_model = {
    .silence_rms = 0.4f,
    .peak_weight = 0.45f,
    .avg_floor = 0.0010f,
    .quiet_band = 0.0003f,
    .sf_smooth = 0.62f,
    .rich_slope = 3.00f,
    .tune_min_rate = 32000,
    .rich_min_rate = 56000,
    .nudge_min_rate = 28000,
    .slope_rate_threshold = 48000,
    .slope_reference_rate = 44100.0f,
    .tilt_nyquist_khz = 24.0f,
    .profiles = {
        { .avg_weight = 0.2f, .loud_exponent = 0.4f, .slope_scale = 1.0f, .peak_floor = 0.0050f },
        { .avg_weight = 0.15f, .loud_exponent = 0.30f, .slope_scale = 3.00f, .peak_floor = 0.0100f },
    },
    .tilt = {
        { 0.5f, 1.00000000f }, { 1.5f, 0.84139514f }, { 3.0f, 0.84139514f }, { 5.0f, 1.00000000f },
        { 8.0f, 1.00000000f }, { 11.0f, 1.41253754f }, { 14.0f, 1.00000000f }, { 18.0f, 0.70794578f },
    },
};

/* Calibrated decision thresholds optimizing BD-Rate, Stereo Coherence, and Transient preservation */
#define ML_SHAPING_ATTACK_STARVED   1.20f
#define ML_SHAPING_ATTACK_MID       0.50f
#define ML_SHAPING_ATTACK_HIGH      0.40f
#define ML_SHAPING_SFM_CUTOFF       0.85f

#define ML_SHAPING_TNS_GAIN_STARVED 1.60f
#define ML_SHAPING_TNS_GAIN_NORMAL  1.40f
#define ML_SHAPING_TNS_GAIN_HIGH    1.35f

#define ML_SHAPING_MS_BIAS_HIGH     1.50f
#define ML_SHAPING_MS_BIAS_MID      1.00f
#define ML_SHAPING_MS_BIAS_NORMAL   1.00f

/* Fast inline decision functions */
static inline bool ml_model_tns_admit(float attack, float sfm, uint32_t bitrate_per_ch)
{
    (void)bitrate_per_ch;
    if (sfm > ML_SHAPING_SFM_CUTOFF)
        return false;

    if (attack <= 0.0f)
        return true;

    return (attack >= ML_SHAPING_ATTACK_MID);
}

static inline float ml_model_tns_gain_threshold(uint32_t bitrate_per_ch)
{
    (void)bitrate_per_ch;
    return ML_SHAPING_TNS_GAIN_NORMAL;
}

static inline float ml_model_stereo_ms_bias(uint32_t bitrate_per_ch, float correlation, bool is_sbr)
{
    if (is_sbr)
        return ML_SHAPING_MS_BIAS_NORMAL;

    /* ML-Optimized M/S bias: at rates >= 64kbps, dynamically adapt to stereo correlation
     * to prevent inter-channel smearing while maximizing energy compaction on coherent stereo. */
    if (bitrate_per_ch >= 64000) {
        if (correlation > 0.80f)
            return ML_SHAPING_MS_BIAS_HIGH;
        else if (correlation < 0.20f)
            return 1.10f; /* Uncorrelated / out-of-phase: relax M/S bias to preserve imaging */
        return 1.35f;
    }

    return ML_SHAPING_MS_BIAS_NORMAL;
}

#ifdef __cplusplus
}
#endif

#endif /* ML_SHAPING_MODEL_H */
