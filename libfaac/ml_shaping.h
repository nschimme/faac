/*
 * FAAC - Freeware Advanced Audio Coder
 * Audio Shaping Machine Learning & Data Logging Infrastructure
 */

#ifndef ML_SHAPING_H
#define ML_SHAPING_H

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include "coder.h"
#include "faac_internal.h"
#include "ml_shaping_model.h"

struct faacEncStruct;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t frame_idx;
    uint16_t channel;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t bitrate_per_ch;
    int block_type;
    float attack_ratio;
    float total_energy;
    float sfm;
    float ms_correlation;
    int tns_enabled;
    int tns_order;
    float tns_gain;
    int ms_used_bands;
    int total_bands;
    float sf_mean;
    float treble_energy_ratio;
} MlShapingTelemetry;

/* Check if telemetry logging is enabled */
bool MlShapingLoggingEnabled(void);

/* Initialize telemetry logger if FAAC_LOG_SHAPING environment variable is set */
void MlShapingInitLogging(const char *csv_path);

/* Record shaping telemetry for a channel frame */
void MlShapingRecordFrame(struct faacEncStruct *hEncoder, CoderInfo *coderInfo, int channel);

/* Log frame shaping telemetry row */
void MlShapingLogTelemetry(const MlShapingTelemetry *record);

/* Close telemetry logger */
void MlShapingCloseLogging(void);

/* Decision: Should TNS be admitted for this frame given attack, SFM, and bit budget?
 * Evaluates ML model heuristics with strict safety bounds. */
bool MlShapingTnsAdmit(float attack, float sfm, uint32_t bitrate_per_ch, int block_type, bool is_lfe);

/* Decision: Optimal TNS prediction gain threshold */
float MlShapingTnsGainThreshold(uint32_t bitrate_per_ch);

/* Decision: M/S Bias adjustment factor based on rate budget and stereo correlation */
float MlShapingStereoMsBias(uint32_t bitrate_per_ch, float correlation, bool is_sbr);

#ifdef __cplusplus
}
#endif

#endif /* ML_SHAPING_H */
