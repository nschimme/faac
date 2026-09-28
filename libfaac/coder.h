/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2001 Menno Bakker
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

#ifndef CODER_H
#define CODER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define FRAME_LEN 1024
#define AAC_MAX_BITS_PER_CH 6144
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

/* Array bounds. FAAC's own TNS analysis (tns.c) only ever emits one filter
 * per long window at a fixed order; these wider bounds exist only for the
 * ladder probe's reemit/injection path, which must represent whatever a
 * reference encoder actually transmitted. LEN_TNS_NFILTL is 2 bits and the
 * field is written as numFilters directly (no bias), so 3 is the true
 * ceiling -- not the 4 an earlier pass used, which silently violated the
 * _Static_assert's own field-width check in channels.c (which this header
 * can't include without a cycle). TNS_MAX_ORDER 20 matches the spec's long-
 * window ceiling and LEN_TNS_ORDERL (5 bits, max 31). */
#define TNS_MAX_ORDER 20
#define TNS_MAX_FILTERS 3
#define DEF_TNS_COEFF_THRESH 0.1f
#define DEF_TNS_COEFF_RES 4
#define DEF_TNS_RES_OFFSET 3

typedef struct {
    int order;                           /* Filter order */
    int direction;                       /* Filtering direction */
    int coefCompress;                    /* Are coeffs compressed? */
    int length;                          /* Length, in bands */
    float aCoeffs[TNS_MAX_ORDER+1];       /* LPC (AR) coefficients */
    int index[TNS_MAX_ORDER+1];          /* Quantized reflection-coeff indices */
} TnsFilterData;

typedef struct {
    int numFilters;                             /* Number of filters */
    int coefResolution;                         /* Coefficient resolution */
    TnsFilterData tnsFilter[TNS_MAX_FILTERS];    /* TNS filters */
} TnsWindowData;

typedef struct {
    int tnsDataPresent;
    int tnsMinBandNumberLong;
    int tnsMaxBandsLong;
    int tnsNumSwbLong;      /* full swb count for the sample rate (decoder's num_swb) */
    int tnsNumSwbShort;     /* short-window counterpart, probe-only (see tns.c TnsInit) */
    TnsWindowData windowData;   /* long-only: one window per frame, not per-short-window */
    int probeSyntax;
    TnsWindowData probeWindowData[MAX_SHORT_WINDOWS];
} TnsInfo;

typedef struct CoderInfo {
    int block_type;
    int desired_block_type;
    int window_shape;
    int prev_window_shape;
    int pulse_count;
    int pulse_start_sfb;
    int pulse_offset[4];
    int pulse_amp[4];

    int global_gain;
    int sf[MAX_SCFAC_BANDS];
    int book[MAX_SCFAC_BANDS];
    int bandcnt;
    int sfbn;
    /* Points at the encoder's prebuilt long or short table (frame.c); the
     * contents depend only on the sample rate and the coded bandwidth, so
     * there is one of each per encoder rather than one per channel. */
    const int *sfb_offset;

    struct {
        int n;
        int len[MAX_SHORT_WINDOWS];
    } groups;

    /* worst case: one codeword with two escapes per two spectral lines */
#define DATASIZE (3*FRAME_LEN/2)

    struct {
        int data;
        int len;
    } s[DATASIZE];
    int datacnt;


    TnsInfo tnsInfo;

    struct CoderInfo *partner;            /* common-window CPE: the right channel, set on the left */

    /* Set by M/S (stereo.c). */
    int useRef;                           /* quantize against refTotal[] */
    float refTotal[MAX_SHORT_WINDOWS];    /* per group: L/R energy before M/S */
    float msEl[MAX_SCFAC_BANDS];          /* M/S band: this channel's L/R energy; 0 = not M/S */
    const float *msPeer;                  /* the other channel's msEl[] */
    uint8_t *msUsed;                      /* the element's ms_used[], set on the left */

    /* Probe-only (core_inject.c): this channel's element-local index (0=left,
     * 1=right of the CPE) and the donor-aligned output frame number, both set
     * once per frame in frame.c. Unused (0) when FAAC_CORE_INJECT is unset. */
    int ciCh;
    int ciFrame;
} CoderInfo;

typedef struct {
  unsigned long sampling_rate;  /* the following entries are for this sampling rate */
  int num_cb_long;
  int num_cb_short;
  uint8_t cb_width_long[NSFB_LONG];
  uint8_t cb_width_short[NSFB_SHORT];
} SR_INFO;

/* Scalefactor-band layout per sampling_rate_index, shared by frame.c and sbr.c. */
extern SR_INFO srInfo[12 + 1];

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CODER_H */
