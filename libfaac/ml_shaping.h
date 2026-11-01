/*
 * FAAC - Freeware Advanced Audio Coder
 * Audio Shaping Machine Learning & Data Logging Infrastructure
 */

#ifndef ML_SHAPING_H
#define ML_SHAPING_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "faac.h"
#include "coder.h"

#ifdef __cplusplus
extern "C" {
#endif

struct faacEncStruct;

/* ML Telemetry record per audio channel per frame */
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

#if defined(FAAC_TELEMETRY) && FAAC_TELEMETRY
/* Check if telemetry logging is enabled */
bool MlShapingLoggingEnabled(void);

/* Initialize telemetry logger to a CSV file */
void MlShapingInitLogging(const char *csv_path);

/* Record per-frame feature telemetry */
void MlShapingRecordFrame(struct faacEncStruct *hEncoder, CoderInfo *coderInfo, int channel);

/* Log telemetry record directly */
void MlShapingLogTelemetry(const MlShapingTelemetry *record);

/* Close telemetry logger */
void MlShapingCloseLogging(void);
#else
static inline bool MlShapingLoggingEnabled(void) { return false; }
static inline void MlShapingInitLogging(const char *csv_path) { (void)csv_path; }
static inline void MlShapingRecordFrame(struct faacEncStruct *hEncoder, CoderInfo *coderInfo, int channel) {
    (void)hEncoder; (void)coderInfo; (void)channel;
}
static inline void MlShapingLogTelemetry(const MlShapingTelemetry *record) { (void)record; }
static inline void MlShapingCloseLogging(void) {}
#endif

#ifdef __cplusplus
}
#endif

#endif /* ML_SHAPING_H */
