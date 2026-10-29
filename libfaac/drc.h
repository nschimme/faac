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

/*
 * Returns the number of bits required for (or written by) dynamic_range_info().
 * If bs is non-NULL, writes the fill_element and dynamic_range_info payload into bs.
 */
int DrcGetBits(const DRCInfo *drc, BitStream *bs);

#if FAAC_ENCODER_DRC
void DrcInit(faacEncStruct *hEncoder);
void DrcProcessFrame(faacEncStruct *hEncoder, float * const *input_pcm, uint32_t samples_per_ch);
void DrcClose(faacEncStruct *hEncoder);
#else
static inline void DrcInit(faacEncStruct *hEncoder) { (void)hEncoder; }
static inline void DrcProcessFrame(faacEncStruct *hEncoder, float * const *input_pcm, uint32_t samples_per_ch)
{
    (void)hEncoder; (void)input_pcm; (void)samples_per_ch;
}
static inline void DrcClose(faacEncStruct *hEncoder) { (void)hEncoder; }
#endif

#ifdef __cplusplus
}
#endif

#endif /* DRC_H */
