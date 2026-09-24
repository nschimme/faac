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

/* Array bounds, sized to what this encoder actually emits rather than to what
 * the spec permits: one filter per long window at a fixed order (tns.c's
 * TNS_LPC_ORDER), never the spec's 4 filters at order 20. Both are checked
 * against the bitstream field widths by _Static_asserts in channels.c, which
 * this header can't include without a cycle. */
#define TNS_MAX_ORDER 8
#define TNS_MAX_FILTERS 1
#define DEF_TNS_COEFF_THRESH 0.1f
#define DEF_TNS_COEFF_RES 4
#define DEF_TNS_RES_OFFSET 3

typedef struct {
    float aCoeffs[TNS_MAX_ORDER+1];         /* LPC (AR) coefficients (36 bytes) */
    int8_t order;                           /* Filter order */
    int8_t direction;                       /* Filtering direction */
    int8_t coefCompress;                    /* Are coeffs compressed? */
    int8_t length;                          /* Length, in bands */
    int8_t index[TNS_MAX_ORDER+1];          /* Quantized reflection-coeff indices (9 bytes) */
    uint8_t pad[3];                         /* Padding to 4-byte boundary */
} TnsFilterData;

typedef struct {
    TnsFilterData tnsFilter[TNS_MAX_FILTERS];/* TNS filters (52 bytes) */
    uint8_t tnsDataPresent;
    uint8_t numFilters;                     /* Number of filters */
    uint8_t coefResolution;                 /* Coefficient resolution */
    uint8_t tnsMinBandNumberLong;
    uint8_t tnsMaxBandsLong;
    uint8_t tnsNumSwbLong;                  /* 6 bytes */
    uint8_t pad[2];                         /* Padding to 4-byte boundary */
} TnsInfo;

typedef struct {
    uint32_t data;
    uint32_t len;
} BitCode;

typedef struct CoderInfo {
    /* 8-byte aligned members */
    /* Points at the encoder's prebuilt long or short table (frame.c); the
     * contents depend only on the sample rate and the coded bandwidth, so
     * there is one of each per encoder rather than one per channel. */
    const int *sfb_offset;

    /* worst case: one codeword with two escapes per two spectral lines */
#define DATASIZE (3*FRAME_LEN/2)
    BitCode s[DATASIZE];                    /* 12288 bytes */

    /* 4-byte aligned members */
    TnsInfo tnsInfo;                         /* 60 bytes */

    /* 2-byte aligned members */
    int16_t sf[MAX_SCFAC_BANDS];             /* 256 bytes */
    int16_t global_gain;                     /* 2 bytes */
    uint16_t bandcnt;                        /* 2 bytes */
    uint16_t datacnt;                        /* 2 bytes */

    /* 1-byte aligned members */
    int8_t book[MAX_SCFAC_BANDS];            /* 128 bytes */
    uint8_t block_type;                      /* 1 byte */
    uint8_t desired_block_type;              /* 1 byte */
    uint8_t sfbn;                            /* 1 byte */

    struct {
        uint8_t n;
        uint8_t len[MAX_SHORT_WINDOWS];
    } groups;                                /* 9 bytes */

    uint8_t pad[3];                         /* Padding to 8-byte boundary */
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
