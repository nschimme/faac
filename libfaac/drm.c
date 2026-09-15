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

/* Thread-safe constant DRM 960-sample scalefactor band tables */
static const SR_INFO srInfoDRM[12 + 1] = {
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
    return (SR_INFO *)&srInfoDRM[sampleRateIdx];
}

static void DRM_FillSineWindow(float *win, int halfLen)
{
    for (int i = 0; i < halfLen; i++)
        win[i] = (float)sin((M_PI_DOUBLE / (2 * halfLen)) * (i + 0.5));
}

void DRM_Init(faacEncStruct *hEncoder)
{
    if (!hEncoder) return;
    DRMContext *ctx = &hEncoder->drmContext;
    if (ctx->drm_sin_long) return; /* already initialized */

    ctx->drm_sin_long = (float*)AllocMemory(480 * 960 * sizeof(float));
    ctx->drm_cos_long = (float*)AllocMemory(480 * 960 * sizeof(float));
    ctx->drm_sin_short = (float*)AllocMemory(60 * 120 * sizeof(float));
    ctx->drm_cos_short = (float*)AllocMemory(60 * 120 * sizeof(float));

    ctx->drm_sin_window_long = (float*)AllocMemory(BLOCK_LEN_LONG_960 * sizeof(float));
    ctx->drm_sin_window_short = (float*)AllocMemory(BLOCK_LEN_SHORT_120 * sizeof(float));

    if (!ctx->drm_sin_long || !ctx->drm_cos_long || !ctx->drm_sin_short || !ctx->drm_cos_short ||
        !ctx->drm_sin_window_long || !ctx->drm_sin_window_short)
        return;

    for (int n = 0; n < 480; n++) {
        for (int k = 0; k < 960; k++) {
            double theta = (M_PI_DOUBLE / 960.0) * (n + 0.5) * (k + 0.5);
            ctx->drm_sin_long[n * 960 + k] = (float)sin(theta);
            ctx->drm_cos_long[n * 960 + k] = (float)cos(theta);
        }
    }

    for (int n = 0; n < 60; n++) {
        for (int k = 0; k < 120; k++) {
            double theta = (M_PI_DOUBLE / 120.0) * (n + 0.5) * (k + 0.5);
            ctx->drm_sin_short[n * 120 + k] = (float)sin(theta);
            ctx->drm_cos_short[n * 120 + k] = (float)cos(theta);
        }
    }

    DRM_FillSineWindow(ctx->drm_sin_window_long, BLOCK_LEN_LONG_960);
    DRM_FillSineWindow(ctx->drm_sin_window_short, BLOCK_LEN_SHORT_120);
}

void DRM_End(faacEncStruct *hEncoder)
{
    if (!hEncoder) return;
    DRMContext *ctx = &hEncoder->drmContext;

    if (ctx->drm_sin_long) { FreeMemory(ctx->drm_sin_long); ctx->drm_sin_long = NULL; }
    if (ctx->drm_cos_long) { FreeMemory(ctx->drm_cos_long); ctx->drm_cos_long = NULL; }
    if (ctx->drm_sin_short) { FreeMemory(ctx->drm_sin_short); ctx->drm_sin_short = NULL; }
    if (ctx->drm_cos_short) { FreeMemory(ctx->drm_cos_short); ctx->drm_cos_short = NULL; }

    if (ctx->drm_sin_window_long) { FreeMemory(ctx->drm_sin_window_long); ctx->drm_sin_window_long = NULL; }
    if (ctx->drm_sin_window_short) { FreeMemory(ctx->drm_sin_window_short); ctx->drm_sin_window_short = NULL; }
}

static void DRM_MDCT(const DRMContext *ctx, const float *data, float *out, int N)
{
    int N2 = N / 2;
    int N4 = N / 4;
    const float *sin_tbl = (N == 1920) ? ctx->drm_sin_long : ctx->drm_sin_short;
    const float *cos_tbl = (N == 1920) ? ctx->drm_cos_long : ctx->drm_cos_short;

    if (!sin_tbl || !cos_tbl) return;

    float tmp[BLOCK_LEN_LONG_960];

    for (int k = 0; k < N2; k++) {
        float s1 = 0.0f, s2 = 0.0f;
        float sign = (k % 2 != 0) ? -1.0f : 1.0f;

        for (int n = 0; n < N4; n++) {
            float f1 = data[N4 - 1 - n] - data[N4 + n];
            float f2 = -data[3 * N4 - 1 - n] - data[3 * N4 + n];
            s1 += f1 * sin_tbl[n * N2 + k];
            s2 += f2 * cos_tbl[n * N2 + k];
        }
        tmp[k] = sign * s1 + s2;
    }
    memcpy(out, tmp, N2 * sizeof(float));
}

static void ApplyWindowSegDRM(float *dst, const float *src, const float *win, int len, bool reverse)
{
    if (reverse) {
        for (int i = 0; i < len; i++) dst[i] = src[i] * win[len - 1 - i];
    } else {
        for (int i = 0; i < len; i++) dst[i] = src[i] * win[i];
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
    const DRMContext *ctx = &hEncoder->drmContext;

    memcpy(overlapBuf, p_prev_data, BLOCK_LEN_LONG_960 * sizeof(float));
    memcpy(overlapBuf + BLOCK_LEN_LONG_960, p_in_data, BLOCK_LEN_LONG_960 * sizeof(float));

    switch (block_type) {
    case ONLY_LONG_WINDOW: {
        ApplyWindowSegDRM(p_out_mdct, overlapBuf, ctx->drm_sin_window_long, BLOCK_LEN_LONG_960, false);
        ApplyWindowSegDRM(p_out_mdct + BLOCK_LEN_LONG_960, overlapBuf + BLOCK_LEN_LONG_960, ctx->drm_sin_window_long, BLOCK_LEN_LONG_960, true);
        DRM_MDCT(ctx, p_out_mdct, p_out_mdct, 2 * BLOCK_LEN_LONG_960);
        break;
    }

    case LONG_SHORT_WINDOW: {
        ApplyWindowSegDRM(p_out_mdct, overlapBuf, ctx->drm_sin_window_long, BLOCK_LEN_LONG_960, false);
        memcpy(p_out_mdct + BLOCK_LEN_LONG_960, overlapBuf + BLOCK_LEN_LONG_960, NFLAT_LS_960 * sizeof(float));
        ApplyWindowSegDRM(p_out_mdct + BLOCK_LEN_LONG_960 + NFLAT_LS_960, overlapBuf + BLOCK_LEN_LONG_960 + NFLAT_LS_960, ctx->drm_sin_window_short, BLOCK_LEN_SHORT_120, true);
        memset(p_out_mdct + BLOCK_LEN_LONG_960 + NFLAT_LS_960 + BLOCK_LEN_SHORT_120, 0, NFLAT_LS_960 * sizeof(float));
        DRM_MDCT(ctx, p_out_mdct, p_out_mdct, 2 * BLOCK_LEN_LONG_960);
        break;
    }

    case SHORT_LONG_WINDOW: {
        memset(p_out_mdct, 0, NFLAT_LS_960 * sizeof(float));
        ApplyWindowSegDRM(p_out_mdct + NFLAT_LS_960, overlapBuf + NFLAT_LS_960, ctx->drm_sin_window_short, BLOCK_LEN_SHORT_120, false);
        memcpy(p_out_mdct + NFLAT_LS_960 + BLOCK_LEN_SHORT_120, overlapBuf + NFLAT_LS_960 + BLOCK_LEN_SHORT_120, NFLAT_LS_960 * sizeof(float));
        ApplyWindowSegDRM(p_out_mdct + BLOCK_LEN_LONG_960, overlapBuf + BLOCK_LEN_LONG_960, ctx->drm_sin_window_long, BLOCK_LEN_LONG_960, true);
        DRM_MDCT(ctx, p_out_mdct, p_out_mdct, 2 * BLOCK_LEN_LONG_960);
        break;
    }

    case ONLY_SHORT_WINDOW: {
        float *src = overlapBuf + NFLAT_LS_960;
        float *dst = p_out_mdct;

        for (int k = 0; k < MAX_SHORT_WINDOWS; k++) {
            ApplyWindowSegDRM(dst, src, ctx->drm_sin_window_short, BLOCK_LEN_SHORT_120, false);
            ApplyWindowSegDRM(dst + BLOCK_LEN_SHORT_120, src + BLOCK_LEN_SHORT_120, ctx->drm_sin_window_short, BLOCK_LEN_SHORT_120, true);
            DRM_MDCT(ctx, dst, dst, 2 * BLOCK_LEN_SHORT_120);

            dst += BLOCK_LEN_SHORT_120;
            src += BLOCK_LEN_SHORT_120;
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
