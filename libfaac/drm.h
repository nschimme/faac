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

#ifndef DRM_H
#define DRM_H

#include <stdint.h>
#include <stdbool.h>
#include "coder.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BLOCK_LEN_LONG_960   960
#define BLOCK_LEN_SHORT_120  120
#define NFLAT_LS_960         420

struct faacEncStruct;

#ifdef FAAC_DRM

void DRM_Init(struct faacEncStruct *hEncoder);
void DRM_End(struct faacEncStruct *hEncoder);

void DRM_FilterBank(struct faacEncStruct *hEncoder,
                    CoderInfo *coderInfo,
                    float *p_prev_data,
                    float *p_in_data,
                    float *p_out_mdct);

SR_INFO *DRM_GetSRInfo(int sampleRateIdx);

uint16_t DRM_CRC16(const uint8_t *data, size_t len);

#else

static inline void DRM_Init(struct faacEncStruct *hEncoder) { (void)hEncoder; }
static inline void DRM_End(struct faacEncStruct *hEncoder) { (void)hEncoder; }

static inline SR_INFO *DRM_GetSRInfo(int sampleRateIdx) { (void)sampleRateIdx; return NULL; }
static inline uint16_t DRM_CRC16(const uint8_t *data, size_t len) { (void)data; (void)len; return 0; }

#endif /* FAAC_DRM */

#ifdef __cplusplus
}
#endif

#endif /* DRM_H */
