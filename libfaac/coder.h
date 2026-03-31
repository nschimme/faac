/****************************************************************************
    Coder information structure

    Copyright (C) 2001 Menno Bakker
    Copyright (C) 2026 Nils Schimmelmann

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.
****************************************************************************/

#ifndef CODER_H
#define CODER_H

#include "faac_real.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FRAME_LEN 1024
#define BLOCK_LEN_LONG 1024
#define BLOCK_LEN_SHORT 128

#define NSFB_LONG  51
#define NSFB_SHORT 15
#define MAX_SHORT_WINDOWS 8
#define MAX_SCFAC_BANDS ((NSFB_SHORT+1)*MAX_SHORT_WINDOWS)

enum WINDOW_TYPE {
    ONLY_LONG_WINDOW,
    LONG_SHORT_WINDOW,
    ONLY_SHORT_WINDOW,
    SHORT_LONG_WINDOW
};

#define TNS_MAX_ORDER 20
#define DEF_TNS_GAIN_THRESH 1.4
#define DEF_TNS_COEFF_THRESH 0.1
#define DEF_TNS_COEFF_RES 4
#define DEF_TNS_RES_OFFSET 3
#define LEN_TNS_NFILTL 2
#define LEN_TNS_NFILTS 1

typedef struct {
    int order;
    int direction;
    int coefCompress;
    int length;
    faac_real aCoeffs[TNS_MAX_ORDER+1];
    faac_real kCoeffs[TNS_MAX_ORDER+1];
    int index[TNS_MAX_ORDER+1];
} TnsFilterData;

typedef struct {
    int numFilters;
    int coefResolution;
    TnsFilterData tnsFilter[1<<LEN_TNS_NFILTL];
} TnsWindowData;

typedef struct {
    int tnsDataPresent;
    int tnsMinBandNumberLong;
    int tnsMinBandNumberShort;
    int tnsMaxBandsLong;
    int tnsMaxBandsShort;
    int tnsMaxOrderLong;
    int tnsMaxOrderShort;
    TnsWindowData windowData[MAX_SHORT_WINDOWS];
} TnsInfo;

typedef struct {
    int window_shape;
    int prev_window_shape;
    int block_type;
    int desired_block_type;

    int global_gain;
    int sf[MAX_SCFAC_BANDS];
    int book[MAX_SCFAC_BANDS];
    int bandcnt;
    int sfbn;
    int sfb_offset[MAX_SCFAC_BANDS + 1];

    struct {
        int n;
        int len[MAX_SHORT_WINDOWS];
    } groups;

#define DATASIZE (4*FRAME_LEN)

    struct {
        int data;
        int len;
    } s[DATASIZE];
    int datacnt;

    TnsInfo tnsInfo;

    /* Buffers for Two-Loop Quantization */
    faac_real xabs[FRAME_LEN];
    int xitab[FRAME_LEN];
    struct {
        int bits;
        float noise;
    } quantCache[MAX_SCFAC_BANDS][512];
    uint8_t quantCacheValid[MAX_SCFAC_BANDS][512];
} CoderInfo;

typedef struct {
  unsigned long sampling_rate;
  int num_cb_long;
  int num_cb_short;
  int cb_width_long[NSFB_LONG];
  int cb_width_short[NSFB_SHORT];
} SR_INFO;

#ifdef __cplusplus
}
#endif

#endif
