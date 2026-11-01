/*
 * FAAC - Freeware Advanced Audio Coder
 * Audio Shaping Machine Learning & Data Logging Infrastructure
 */

#include "ml_shaping.h"
#include "frame.h"
#include "blockswitch.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#if defined(FAAC_TELEMETRY) && FAAC_TELEMETRY
static FILE *s_log_file = NULL;
static bool s_logging_initialized = false;
static bool s_logging_enabled = false;
static int s_active_instances = 0;

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
    s_active_instances++;
    if (!s_log_file && csv_path && strlen(csv_path) > 0) {
        s_log_file = fopen(csv_path, "w");
        if (s_log_file) {
            s_logging_enabled = true;
            /* Header */
            fprintf(s_log_file,
                "frame_idx,channel,num_channels,sample_rate,bitrate_per_ch,"
                "block_type,attack_ratio,total_energy,sfm,ms_correlation,"
                "tns_enabled,tns_order,tns_gain,ms_used_bands,total_bands,sf_mean,treble_energy_ratio\n");
            fflush(s_log_file);
        } else {
            s_logging_enabled = false;
        }
    }
    s_logging_initialized = true;
}

void MlShapingRecordFrame(struct faacEncStruct *hEncoder, CoderInfo *coderInfo, int channel)
{
    if (!s_logging_enabled || !s_log_file || !hEncoder || !coderInfo)
        return;

    MlShapingTelemetry record;
    memset(&record, 0, sizeof(record));
    record.frame_idx = hEncoder->frameNum;
    record.channel = (uint16_t)channel;
    record.num_channels = (uint16_t)hEncoder->numChannels;
    record.sample_rate = (uint32_t)hEncoder->sampleRate;
    record.bitrate_per_ch = hEncoder->numChannels ? (uint32_t)(hEncoder->config.bitRate / hEncoder->numChannels) : 0;
    record.block_type = coderInfo[channel].block_type;
    record.attack_ratio = PsyGetAttack(&hEncoder->psyInfo[channel]);
    record.tns_enabled = coderInfo[channel].tnsInfo.tnsDataPresent;
    record.tns_order = coderInfo[channel].tnsInfo.windowData.numFilters > 0 ?
                       coderInfo[channel].tnsInfo.windowData.tnsFilter[0].order : 0;
    record.total_bands = coderInfo[channel].sfbn;

    /* Compute energy, treble energy ratio, and spectral flatness (SFM) */
    float sum_energy_ch = 0.0f;
    float sum_treble_energy = 0.0f;
    float sum_log_e_ch = 0.0f;
    int spec_len = (record.block_type == ONLY_SHORT_WINDOW) ? (BLOCK_LEN_SHORT * 8) : BLOCK_LEN_LONG;
    int treble_start = spec_len / 2;
    for (int i = 0; i < spec_len; i++) {
        float val = hEncoder->freqBuff[channel][i];
        float e = val * val;
        sum_energy_ch += e;
        if (i >= treble_start)
            sum_treble_energy += e;
        sum_log_e_ch += logf(e > 1e-12f ? e : 1e-12f);
    }
    record.total_energy = sum_energy_ch;
    record.treble_energy_ratio = (sum_energy_ch > 1e-12f) ? (sum_treble_energy / sum_energy_ch) : 0.0f;
    if (spec_len > 0 && sum_energy_ch > 1e-12f) {
        float gm = expf(sum_log_e_ch / (float)spec_len);
        float am = sum_energy_ch / (float)spec_len;
        record.sfm = (am > 1e-12f) ? (gm / am) : 0.0f;
    }

    /* Scalefactor distribution */
    if (record.total_bands > 0) {
        float sum_sf = 0.0f;
        for (int b = 0; b < record.total_bands; b++)
            sum_sf += coderInfo[channel].sf[b];
        record.sf_mean = sum_sf / (float)record.total_bands;
    }

    /* Stereo M/S stats */
    if (hEncoder->numChannels >= 2 && channel == 0 && hEncoder->elements[0].type == ID_CPE) {
        int ms_count = 0;
        for (int b = 0; b < record.total_bands; b++) {
            if (hEncoder->elements[0].msInfo.ms_used[b])
                ms_count++;
        }
        record.ms_used_bands = ms_count;
    }

    MlShapingLogTelemetry(&record);
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

    float sf_mean = isfinite(record->sf_mean) ? record->sf_mean : 0.0f;
    float treble_r = isfinite(record->treble_energy_ratio) ? record->treble_energy_ratio : 0.0f;

    fprintf(s_log_file,
        "%u,%u,%u,%u,%u,%d,%.5f,%.5e,%.5f,%.5f,%d,%d,%.4f,%d,%d,%.2f,%.4f\n",
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
        record->total_bands,
        sf_mean,
        treble_r);
}

void MlShapingCloseLogging(void)
{
    if (s_active_instances > 0)
        s_active_instances--;

    if (s_active_instances == 0 && s_log_file) {
        fclose(s_log_file);
        s_log_file = NULL;
        s_logging_enabled = false;
        s_logging_initialized = false;
    }
}
#endif
