/*
 * FAAC - Freeware Advanced Audio Coder
 * Huffman coding per ISO/IEC 14496-3
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

#ifndef HUFF2_H
#define HUFF2_H

#include <stdint.h>
#include "bitstream.h"
#include "coder.h"

/* Huffman Codebook (HCB) indices as per ISO/IEC 14496-3 §4.6.3 */
enum {
    HCB_ZERO = 0,
    HCB_1, HCB_2, HCB_3, HCB_4, HCB_5, HCB_6,
    HCB_7, HCB_8, HCB_9, HCB_10,
    HCB_ESC = 11,
    HCB_DELTA = 12,
    HCB_PNS = 13,
    HCB_INTENSITY2 = 14,
    HCB_INTENSITY = 15,
    HCB_NONE
};

/* Largest Absolute Values (LAV) for each codebook pair */
enum {
    LAV_1 = 1,
    LAV_2 = 2,
    LAV_4 = 4,
    LAV_7 = 7,
    LAV_12 = 12,
    LAV_ESC = 16
};

/* Table dimensions for index calculation */
enum {
    DIM_S4 = 3,
    DIM_M4 = 3,
    DIM_S2 = 9,
    DIM_M2_7 = 8,
    DIM_M2_12 = 13,
    DIM_ESC = 17
};

/* 13-bit escape field limit */
#define MAX_HUFF_ESC_VAL 8191

/* Section merging optimization threshold */
#define CROSS_MERGE_MAX_LINES 16

/* Scalefactor step constants (ISO 14496-3 §8.3.4): one SF unit = 2^(1/4) in amplitude.
 * AMPL converts a log10 amplitude ratio to scalefactor index steps (= 4/log10(2)).
 * ENRG is AMPL/2 — use when the input ratio is energy (squared amplitude). */
#define SF_STEP_AMPL  13.287712379549461f
#define SF_STEP_ENRG  (SF_STEP_AMPL / 2.0f)

enum {
    SF_OFFSET = 100,
    SF_MIN = 10,
    SF_PNS_OFFSET = SF_OFFSET - SF_MIN,
    SF_DELTA = 60,
    SF_MAX_ABS = 255
};

/* CoderScratch: transient state for section merging and deferred serialization.
   Using uint16_t fields and short-lived allocation to stay within library size limits. */
typedef struct {
    int16_t quant[FRAME_LEN];
    int quantcnt;
    struct {
        uint16_t off;
        uint16_t maxq;
        uint16_t bits;
        uint16_t altbits;
    } bandmeta[MAX_SCFAC_BANDS];
} CoderScratch;

/* Bound scalefactor deltas to the +/- 60 range of HCB_DELTA (book 12) */
static inline int clamp_sf_diff(int diff)
{
    if (diff > SF_DELTA)  return SF_DELTA;
    if (diff < -SF_DELTA) return -SF_DELTA;
    return diff;
}

int huffbook(CoderInfo *coder, CoderScratch *scratch, int *qs, int len, int maxq);
void MergeSections(CoderInfo *coder, CoderScratch *scratch);
void SerializeSpectralData(CoderInfo *coder, CoderScratch *scratch);
int writebooks(CoderInfo *coder, BitStream *stream, int writeFlag);
int writesf(CoderInfo *coder, BitStream *bitStream, int writeFlag);

#endif
