/*
 * FAAC - Freeware Advanced Audio Coder
 * Audio Shaping Machine Learning & Data Logging Infrastructure
 */

#include "ml_shaping.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static FILE *s_log_file = NULL;
static bool s_logging_initialized = false;
static bool s_logging_enabled = false;

bool MlShapingLoggingEnabled(void)
{
    if (!s_logging_initialized) {
        const char *env_path = getenv("FAAC_LOG_SHAPING");
        if (env_path && strlen(env_path) > 0) {
            MlShapingInitLogging(env_path);
        } else {
            s_logging_initialized = true;
            s_logging_enabled = false;
        }
    }
    return s_logging_enabled;
}

void MlShapingInitLogging(const char *csv_path)
{
    if (s_log_file) {
        fclose(s_log_file);
        s_log_file = NULL;
    }

    if (csv_path && strlen(csv_path) > 0) {
        s_log_file = fopen(csv_path, "w");
        if (s_log_file) {
            s_logging_enabled = true;
            /* Header */
            fprintf(s_log_file,
                "frame_idx,channel,num_channels,sample_rate,bitrate_per_ch,"
                "block_type,attack_ratio,total_energy,sfm,ms_correlation,"
                "tns_enabled,tns_order,tns_gain,ms_used_bands,total_bands\n");
            fflush(s_log_file);
        } else {
            s_logging_enabled = false;
        }
    } else {
        s_logging_enabled = false;
    }
    s_logging_initialized = true;
}

void MlShapingLogTelemetry(const MlShapingTelemetry *record)
{
    if (!s_logging_enabled || !s_log_file || !record)
        return;

    /* Prevent NaN/Inf output in CSV */
    float attack = isfinite(record->attack_ratio) ? record->attack_ratio : 0.0f;
    float energy = isfinite(record->total_energy) ? record->total_energy : 0.0f;
    float sfm = isfinite(record->sfm) ? record->sfm : 0.0f;
    float corr = isfinite(record->ms_correlation) ? record->ms_correlation : 0.0f;
    float gain = isfinite(record->tns_gain) ? record->tns_gain : 0.0f;

    fprintf(s_log_file,
        "%u,%u,%u,%u,%u,%d,%.5f,%.5e,%.5f,%.5f,%d,%d,%.4f,%d,%d\n",
        record->frame_idx,
        record->channel,
        record->num_channels,
        record->sample_rate,
        record->bitrate_per_ch,
        record->block_type,
        attack,
        energy,
        sfm,
        corr,
        record->tns_enabled,
        record->tns_order,
        gain,
        record->ms_used_bands,
        record->total_bands);
}

void MlShapingCloseLogging(void)
{
    if (s_log_file) {
        fclose(s_log_file);
        s_log_file = NULL;
    }
    s_logging_enabled = false;
    s_logging_initialized = false;
}

bool MlShapingTnsAdmit(float attack, float sfm, uint32_t bitrate_per_ch, int block_type, bool is_lfe)
{
    if (is_lfe || block_type == ONLY_SHORT_WINDOW)
        return false;

    return ml_model_tns_admit(attack, sfm, bitrate_per_ch);
}

float MlShapingTnsGainThreshold(uint32_t bitrate_per_ch)
{
    return ml_model_tns_gain_threshold(bitrate_per_ch);
}

float MlShapingStereoMsBias(uint32_t bitrate_per_ch, float correlation, bool is_sbr)
{
    return ml_model_stereo_ms_bias(bitrate_per_ch, correlation, is_sbr);
}
