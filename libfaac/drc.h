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

#ifndef DRC_H
#define DRC_H

#include <stdbool.h>
#include <stdint.h>

#define EXT_DYNAMIC_RANGE 0xB

/* Sliding window parameter constants */
#define DRC_SUBBLOCK_DUR_SEC          0.100f  /* 100 ms sub-blocks */
#define DRC_NUM_SUBBLOCKS_3S          30      /* 3.0 s short-term window (30 x 100 ms) */
#define DRC_NUM_SUBBLOCKS_400MS       4       /* 400 ms momentary window (4 x 100 ms) */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BitStream BitStream;
typedef struct faacEncStruct faacEncStruct;

typedef struct DRCInfo {
    bool  enabled;
    float gain_db;                 /* -31.75 to +31.75 dB */
    bool  prog_ref_level_present;
    float prog_ref_level_db;       /* -31.75 to 0.0 dBFS */
} DRCInfo;

typedef struct BiquadState {
    float v1_1, v2_1; /* Pre-filter delay elements */
    float v1_2, v2_2; /* RLB filter delay elements */
    float weight;     /* BS.1770-4 channel weight (1.0 for main channels, 1.41 for surround, 0.0 for LFE) */
} BiquadState;

typedef struct DrcAnalyzer {
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
} DrcAnalyzer;

/*
 * Returns the number of bits required for (or written by) dynamic_range_info().
 * If bs is non-NULL, writes the fill_element and dynamic_range_info payload into bs.
 */
int DrcGetBits(const DRCInfo *drc, BitStream *bs);

#if FAAC_ENCODER_DRC
void DrcInit(faacEncStruct *hEncoder);
void DrcProcessFrame(faacEncStruct *hEncoder, float * const *input_pcm, uint32_t samples_per_ch);
void DrcClose(faacEncStruct *hEncoder);
bool DrcActive(const faacEncStruct *hEncoder);
#else
static inline void DrcInit(faacEncStruct *hEncoder) { (void)hEncoder; }
static inline void DrcProcessFrame(faacEncStruct *hEncoder, float * const *input_pcm, uint32_t samples_per_ch)
{
    (void)hEncoder; (void)input_pcm; (void)samples_per_ch;
}
static inline void DrcClose(faacEncStruct *hEncoder) { (void)hEncoder; }
static inline bool DrcActive(const faacEncStruct *hEncoder) { (void)hEncoder; return false; }
#endif

#ifdef __cplusplus
}
#endif

#endif /* DRC_H */
