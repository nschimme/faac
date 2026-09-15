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

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "drm.h"
#ifdef FAAC_DRM
#include "frame.h"
#include "blockswitch.h"
#include "stereo.h"
#include "filtbank.h"
#include "util.h"

/* DRM 960-sample scalefactor band tables for all 12 sampling rate indices */
static SR_INFO srInfoDRM[12 + 1] = {
    { 96000, 40, 12,
        { 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 8, 8, 8, 12, 12, 12, 12, 12, 16, 16, 24, 28, 36, 44, 64, 64, 64, 64, 64, 64, 64, 64, 64, 32 },
        { 4, 4, 4, 4, 4, 4, 8, 8, 8, 16, 28, 28 }
    },
    { 88200, 40, 12,
        { 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 8, 8, 8, 12, 12, 12, 12, 12, 16, 16, 24, 28, 36, 44, 64, 64, 64, 64, 64, 64, 64, 64, 64, 32 },
        { 4, 4, 4, 4, 4, 4, 8, 8, 8, 16, 28, 28 }
    },
    { 64000, 46, 12,
        { 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 8, 8, 12, 12, 12, 16, 16, 16, 20, 24, 24, 28, 36, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 16 },
        { 4, 4, 4, 4, 4, 4, 8, 8, 8, 16, 28, 24 }
    },
    { 48000, 49, 14,
        { 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 8, 8, 8, 8, 8, 12, 12, 12, 12, 16, 16, 20, 20, 24, 24, 28, 28, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32 },
        { 4, 4, 4, 4, 4, 8, 8, 8, 12, 12, 12, 16, 16, 8 }
    },
    { 44100, 49, 14,
        { 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 8, 8, 8, 8, 8, 12, 12, 12, 12, 16, 16, 20, 20, 24, 24, 28, 28, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32 },
        { 4, 4, 4, 4, 4, 8, 8, 8, 12, 12, 12, 16, 16, 8 }
    },
    { 32000, 49, 14,
        { 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 8, 8, 8, 8, 8, 12, 12, 12, 12, 16, 16, 20, 20, 24, 24, 28, 28, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32 },
        { 4, 4, 4, 4, 4, 8, 8, 8, 12, 12, 12, 16, 16, 8 }
    },
    { 24000, 46, 15,
        { 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 12, 12, 12, 12, 16, 16, 16, 20, 20, 24, 24, 28, 28, 32, 36, 36, 40, 44, 48, 52, 52, 64, 64, 64, 64 },
        { 4, 4, 4, 4, 4, 4, 4, 8, 8, 8, 12, 12, 16, 16, 12 }
    },
    { 22050, 46, 15,
        { 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 12, 12, 12, 12, 16, 16, 16, 20, 20, 24, 24, 28, 28, 32, 36, 36, 40, 44, 48, 52, 52, 64, 64, 64, 64 },
        { 4, 4, 4, 4, 4, 4, 4, 8, 8, 8, 12, 12, 16, 16, 12 }
    },
    { 16000, 42, 15,
        { 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 12, 12, 12, 12, 12, 12, 12, 12, 12, 16, 16, 16, 16, 20, 20, 20, 24, 24, 28, 28, 32, 36, 40, 40, 44, 48, 52, 56, 60, 64, 64 },
        { 4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 12, 12, 16, 20, 12 }
    },
    { 12000, 42, 15,
        { 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 12, 12, 12, 12, 12, 12, 12, 12, 12, 16, 16, 16, 16, 20, 20, 20, 24, 24, 28, 28, 32, 36, 40, 40, 44, 48, 52, 56, 60, 64, 64 },
        { 4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 12, 12, 16, 20, 12 }
    },
    { 11025, 42, 15,
        { 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 12, 12, 12, 12, 12, 12, 12, 12, 12, 16, 16, 16, 16, 20, 20, 20, 24, 24, 28, 28, 32, 36, 40, 40, 44, 48, 52, 56, 60, 64, 64 },
        { 4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 12, 12, 16, 20, 12 }
    },
    { 8000, 40, 15,
        { 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 16, 16, 16, 16, 16, 16, 16, 20, 20, 20, 20, 24, 24, 24, 28, 28, 32, 36, 36, 40, 44, 48, 52, 56, 60, 64, 16 },
        { 4, 4, 4, 4, 4, 4, 4, 8, 8, 8, 8, 12, 16, 20, 12 }
    },
    { 0, 0, 0, {0}, {0} }
};

SR_INFO *DRM_GetSRInfo(int sampleRateIdx)
{
    if (sampleRateIdx < 0 || sampleRateIdx >= 12) return NULL;
    return &srInfoDRM[sampleRateIdx];
}

/* Precomputed trigonometric tables for DRM MDCT 960 and 120 */
static float *drm_sin_long = NULL;  /* 480 * 960 */
static float *drm_cos_long = NULL;  /* 480 * 960 */
static float *drm_sin_short = NULL; /* 60 * 120 */
static float *drm_cos_short = NULL; /* 60 * 120 */

/* Dedicated 960 and 120 window tables for Sine and KBD window shapes */
static float *drm_sin_window_long = NULL;  /* 960 */
static float *drm_sin_window_short = NULL; /* 120 */
static float *drm_kbd_window_long = NULL;  /* 960 */
static float *drm_kbd_window_short = NULL; /* 120 */

static double DRM_BesselI0(double x)
{
    const double tolerance = DBL_EPSILON;
    double halfX = x * 0.5;
    double term = 1.0;
    double series = 1.0;
    int k = 1;

    do {
        double ratio = halfX / (double)k;
        term *= ratio * ratio;
        series += term;
        k++;
    } while (term > tolerance * series);

    return series;
}

static void DRM_FillSineWindow(float *win, int halfLen)
{
    int i;
    for (i = 0; i < halfLen; i++)
        win[i] = (float)sin((M_PI_DOUBLE / (2 * halfLen)) * (i + 0.5));
}

static void DRM_FillKbdWindow(float *win, int halfLen, double alpha)
{
    const double omega = alpha * M_PI_DOUBLE / (double)halfLen;
    const double alpha2 = 4.0 * omega * omega;
    const int quarterLen = halfLen / 2;
    double shapeTerm[BLOCK_LEN_LONG_960 / 2 + 1];
    double weightedTotal = 0.0;
    double running = 0.0;
    double scale;
    int i;

    for (i = 0; i <= quarterLen; i++) {
        double symmetric = (double)i * (double)(halfLen - i) * alpha2;
        int isInterior = (i > 0) && (i < quarterLen);

        shapeTerm[i] = DRM_BesselI0(sqrt(symmetric));
        weightedTotal += shapeTerm[i] * (isInterior ? 2 : 1);
    }
    scale = 1.0 / (weightedTotal + 1.0);

    for (i = 0; i < halfLen; i++) {
        int idx = (i <= quarterLen) ? i : (halfLen - i);
        running += shapeTerm[idx];
        win[i] = (float)sqrt(running * scale);
    }
}

void DRM_Init(faacEncStruct *hEncoder)
{
    (void)hEncoder;
    if (drm_sin_long) return; /* already initialized */

    drm_sin_long = (float*)AllocMemory(480 * 960 * sizeof(float));
    drm_cos_long = (float*)AllocMemory(480 * 960 * sizeof(float));
    drm_sin_short = (float*)AllocMemory(60 * 120 * sizeof(float));
    drm_cos_short = (float*)AllocMemory(60 * 120 * sizeof(float));

    drm_sin_window_long = (float*)AllocMemory(BLOCK_LEN_LONG_960 * sizeof(float));
    drm_sin_window_short = (float*)AllocMemory(BLOCK_LEN_SHORT_120 * sizeof(float));
    drm_kbd_window_long = (float*)AllocMemory(BLOCK_LEN_LONG_960 * sizeof(float));
    drm_kbd_window_short = (float*)AllocMemory(BLOCK_LEN_SHORT_120 * sizeof(float));

    if (!drm_sin_long || !drm_cos_long || !drm_sin_short || !drm_cos_short ||
        !drm_sin_window_long || !drm_sin_window_short || !drm_kbd_window_long || !drm_kbd_window_short)
        return;

    for (int n = 0; n < 480; n++) {
        for (int k = 0; k < 960; k++) {
            double theta = (M_PI_DOUBLE / 960.0) * (n + 0.5) * (k + 0.5);
            drm_sin_long[n * 960 + k] = (float)sin(theta);
            drm_cos_long[n * 960 + k] = (float)cos(theta);
        }
    }

    for (int n = 0; n < 60; n++) {
        for (int k = 0; k < 120; k++) {
            double theta = (M_PI_DOUBLE / 120.0) * (n + 0.5) * (k + 0.5);
            drm_sin_short[n * 120 + k] = (float)sin(theta);
            drm_cos_short[n * 120 + k] = (float)cos(theta);
        }
    }

    DRM_FillSineWindow(drm_sin_window_long, BLOCK_LEN_LONG_960);
    DRM_FillKbdWindow(drm_kbd_window_long, BLOCK_LEN_LONG_960, 4.0);
    DRM_FillSineWindow(drm_sin_window_short, BLOCK_LEN_SHORT_120);
    DRM_FillKbdWindow(drm_kbd_window_short, BLOCK_LEN_SHORT_120, 6.0);
}

void DRM_End(faacEncStruct *hEncoder)
{
    (void)hEncoder;
    if (drm_sin_long) { FreeMemory(drm_sin_long); drm_sin_long = NULL; }
    if (drm_cos_long) { FreeMemory(drm_cos_long); drm_cos_long = NULL; }
    if (drm_sin_short) { FreeMemory(drm_sin_short); drm_sin_short = NULL; }
    if (drm_cos_short) { FreeMemory(drm_cos_short); drm_cos_short = NULL; }

    if (drm_sin_window_long) { FreeMemory(drm_sin_window_long); drm_sin_window_long = NULL; }
    if (drm_sin_window_short) { FreeMemory(drm_sin_window_short); drm_sin_window_short = NULL; }
    if (drm_kbd_window_long) { FreeMemory(drm_kbd_window_long); drm_kbd_window_long = NULL; }
    if (drm_kbd_window_short) { FreeMemory(drm_kbd_window_short); drm_kbd_window_short = NULL; }
}

static void DRM_MDCT(const float *data, float *out, int N)
{
    int N2 = N / 2;
    int N4 = N / 4;
    const float *sin_tbl = (N == 1920) ? drm_sin_long : drm_sin_short;
    const float *cos_tbl = (N == 1920) ? drm_cos_long : drm_cos_short;

    if (!sin_tbl || !cos_tbl) return;

    for (int k = 0; k < N2; k++) {
        float s1 = 0.0f, s2 = 0.0f;
        float sign = (k % 2 != 0) ? -1.0f : 1.0f;
        int n;

        for (n = 0; n < N4; n++) {
            float f1 = data[N4 - 1 - n] - data[N4 + n];
            float f2 = -data[3 * N4 - 1 - n] - data[3 * N4 + n];
            s1 += f1 * sin_tbl[n * N2 + k];
            s2 += f2 * cos_tbl[n * N2 + k];
        }
        out[k] = sign * s1 + s2;
    }
}

static const float *SelectDRMWindow(int shape, bool isLong)
{
    if (shape == KBD_WINDOW)
        return isLong ? drm_kbd_window_long : drm_kbd_window_short;
    return isLong ? drm_sin_window_long : drm_sin_window_short;
}

static void ApplyWindowSegDRM(float *dst, const float *src, const float *win, int len, bool reverse)
{
    int i;
    if (reverse) {
        for (i = 0; i < len; i++) dst[i] = src[i] * win[len - 1 - i];
    } else {
        for (i = 0; i < len; i++) dst[i] = src[i] * win[i];
    }
}

void DRM_FilterBank(faacEncStruct* hEncoder,
                    CoderInfo *coderInfo,
                    float *p_prev_data,
                    float *p_in_data,
                    float *p_out_mdct)
{
    float *overlapBuf = hEncoder->gpsyInfo.sharedWorkBuffLong;
    int block_type = coderInfo->block_type;
    const float *leftWin, *rightWin;
    int k;

    memcpy(overlapBuf, p_prev_data, BLOCK_LEN_LONG_960 * sizeof(float));
    memcpy(overlapBuf + BLOCK_LEN_LONG_960, p_in_data, BLOCK_LEN_LONG_960 * sizeof(float));

    switch (block_type) {
    case ONLY_LONG_WINDOW: {
        leftWin  = SelectDRMWindow(coderInfo->prev_window_shape, true);
        rightWin = SelectDRMWindow(coderInfo->window_shape, true);

        ApplyWindowSegDRM(p_out_mdct, overlapBuf, leftWin, BLOCK_LEN_LONG_960, false);
        ApplyWindowSegDRM(p_out_mdct + BLOCK_LEN_LONG_960, overlapBuf + BLOCK_LEN_LONG_960, rightWin, BLOCK_LEN_LONG_960, true);
        DRM_MDCT(p_out_mdct, p_out_mdct, 2 * BLOCK_LEN_LONG_960);
        break;
    }

    case LONG_SHORT_WINDOW: {
        leftWin  = SelectDRMWindow(coderInfo->prev_window_shape, true);
        rightWin = SelectDRMWindow(coderInfo->window_shape, false);

        ApplyWindowSegDRM(p_out_mdct, overlapBuf, leftWin, BLOCK_LEN_LONG_960, false);
        memcpy(p_out_mdct + BLOCK_LEN_LONG_960, overlapBuf + BLOCK_LEN_LONG_960, NFLAT_LS_960 * sizeof(float));
        ApplyWindowSegDRM(p_out_mdct + BLOCK_LEN_LONG_960 + NFLAT_LS_960, overlapBuf + BLOCK_LEN_LONG_960 + NFLAT_LS_960, rightWin, BLOCK_LEN_SHORT_120, true);
        memset(p_out_mdct + BLOCK_LEN_LONG_960 + NFLAT_LS_960 + BLOCK_LEN_SHORT_120, 0, NFLAT_LS_960 * sizeof(float));
        DRM_MDCT(p_out_mdct, p_out_mdct, 2 * BLOCK_LEN_LONG_960);
        break;
    }

    case SHORT_LONG_WINDOW: {
        leftWin  = SelectDRMWindow(coderInfo->prev_window_shape, false);
        rightWin = SelectDRMWindow(coderInfo->window_shape, true);

        memset(p_out_mdct, 0, NFLAT_LS_960 * sizeof(float));
        ApplyWindowSegDRM(p_out_mdct + NFLAT_LS_960, overlapBuf + NFLAT_LS_960, leftWin, BLOCK_LEN_SHORT_120, false);
        memcpy(p_out_mdct + NFLAT_LS_960 + BLOCK_LEN_SHORT_120, overlapBuf + NFLAT_LS_960 + BLOCK_LEN_SHORT_120, NFLAT_LS_960 * sizeof(float));
        ApplyWindowSegDRM(p_out_mdct + BLOCK_LEN_LONG_960, overlapBuf + BLOCK_LEN_LONG_960, rightWin, BLOCK_LEN_LONG_960, true);
        DRM_MDCT(p_out_mdct, p_out_mdct, 2 * BLOCK_LEN_LONG_960);
        break;
    }

    case ONLY_SHORT_WINDOW: {
        float *src = overlapBuf + NFLAT_LS_960;
        float *dst = p_out_mdct;

        leftWin  = SelectDRMWindow(coderInfo->prev_window_shape, false);
        rightWin = SelectDRMWindow(coderInfo->window_shape, false);

        for (k = 0; k < MAX_SHORT_WINDOWS; k++) {
            ApplyWindowSegDRM(dst, src, leftWin, BLOCK_LEN_SHORT_120, false);
            ApplyWindowSegDRM(dst + BLOCK_LEN_SHORT_120, src + BLOCK_LEN_SHORT_120, rightWin, BLOCK_LEN_SHORT_120, true);
            DRM_MDCT(dst, dst, 2 * BLOCK_LEN_SHORT_120);

            dst += BLOCK_LEN_SHORT_120;
            src += BLOCK_LEN_SHORT_120;
            leftWin = rightWin;
        }
        break;
    }
    }
}

/* ETSI ES 201 980 CRC-16 calculation (polynomial 0x8005: x^16 + x^15 + x^2 + 1) */
uint16_t DRM_CRC16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= ((uint16_t)data[i]) << 8;
        for (int b = 0; b < 8; b++) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x8005;
            else
                crc <<= 1;
        }
    }
    return crc;
}

#endif /* FAAC_DRM */
