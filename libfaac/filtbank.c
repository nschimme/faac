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

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <string.h>

#include "coder.h"
#include "filtbank.h"
#include "frame.h"
#include "fft.h"
#include "util.h"

/* Sine and Kaiser-Bessel-Derived windows, ISO/IEC 13818-7 Annex 4.6.4.
 * KBD argument uses the product form i*(halfLen-i) (a difference-of-
 * squares factoring of the spec's (2i/halfLen-1)^2 shape term) so the
 * Bessel argument is one multiply instead of a subtract-then-square.
 * These tables are built once at encoder init, so the series and
 * normalization run in double (rounding to float only when stored into
 * win[]) for a correctly-rounded table at zero runtime cost. */

static double BesselI0(double x)
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

static void FillSineWindow(float *win, int halfLen)
{
    int i;

    for (i = 0; i < halfLen; i++)
        win[i] = (float)sin((M_PI_DOUBLE / (2 * halfLen)) * (i + 0.5));
}

static void FillKbdWindow(float *win, int halfLen, double alpha)
{
    const double omega = alpha * M_PI_DOUBLE / (double)halfLen;
    const double alpha2 = 4.0 * omega * omega;
    const int quarterLen = halfLen / 2;
    double shapeTerm[BLOCK_LEN_LONG / 2 + 1];
    double weightedTotal = 0.0;
    double running = 0.0;
    double scale;
    int i;

    /* Symmetric around quarterLen, so interior terms count twice below. */
    for (i = 0; i <= quarterLen; i++) {
        double symmetric = (double)i * (double)(halfLen - i) * alpha2;
        int isInterior = (i > 0) && (i < quarterLen);

        shapeTerm[i] = BesselI0(sqrt(symmetric));
        weightedTotal += shapeTerm[i] * (isInterior ? 2 : 1);
    }
    scale = 1.0 / (weightedTotal + 1.0);

    for (i = 0; i <= quarterLen; i++) {
        running += shapeTerm[i];
        win[i] = (float)sqrt(running * scale);
    }
    /* Past the midpoint, reuse the mirrored term instead of recomputing it. */
    for (; i < halfLen; i++) {
        running += shapeTerm[halfLen - i];
        win[i] = (float)sqrt(running * scale);
    }
}

typedef struct {
    float *sine;
    float *kbd;
} WindowPair;

static void BuildWindowPair(WindowPair *wp, int halfLen, double kbdAlpha)
{
    FillSineWindow(wp->sine, halfLen);
    FillKbdWindow(wp->kbd, halfLen, kbdAlpha);
}

/* Fuses MDCT pre-twiddle directly into stage 0 of Radix-4 DIF.
 * Normative reference: ISO/IEC 14496-3 Section 4.6.4.
 * This avoids intermediate scratchpad round-trips for N4 complex floats.
 */
static inline void fused_pretwiddle_stage0(
    const float * restrict data,
    int N,
    float * restrict xr,
    float * restrict xi,
    const fftfloat * restrict cosT,
    const fftfloat * restrict sinT,
    const fftfloat * restrict costbl,
    const fftfloat * restrict sintbl)
{
    assert(data != NULL);
    assert(xr != NULL);
    assert(xi != NULL);
    assert(cosT != NULL);
    assert(sinT != NULL);
    assert(costbl != NULL);
    assert(sintbl != NULL);
    assert(N == 2 * BLOCK_LEN_SHORT || N == 2 * BLOCK_LEN_LONG);

    const int N2 = N >> 1;
    const int N4 = N >> 2;
    const int n2 = N4 >> 2; /* stage 0: n2 = N4 / 4 */

    int j;

    /* Loop over j to compute pre-twiddles and immediately apply Radix-4 butterfly */
    for (j = 0; j < n2; j++) {
        int idx1 = j;
        int idx2 = j + n2;
        int idx3 = j + 2 * n2;
        int idx4 = j + 3 * n2;

        /* Indices idx1 and idx2 are always < N8; idx3 and idx4 are always >= N8. */
        /* idx1 */
        int n1_1 = N2 - 1 - 2*idx1;
        int n2_1 = 2*idx1;
        float fRe1 = data[N4 + n1_1] + data[N + N4 - 1 - n1_1];
        float fIm1 = data[N4 + n2_1] - data[N4 - 1 - n2_1];
        float r1 = fRe1 * cosT[idx1] + fIm1 * sinT[idx1];
        float i1 = fIm1 * cosT[idx1] - fRe1 * sinT[idx1];

        /* idx2 */
        int n1_2 = N2 - 1 - 2*idx2;
        int n2_2 = 2*idx2;
        float fRe2 = data[N4 + n1_2] + data[N + N4 - 1 - n1_2];
        float fIm2 = data[N4 + n2_2] - data[N4 - 1 - n2_2];
        float r2 = fRe2 * cosT[idx2] + fIm2 * sinT[idx2];
        float i2 = fIm2 * cosT[idx2] - fRe2 * sinT[idx2];

        /* idx3 */
        int n1_3 = N2 - 1 - 2*idx3;
        int n2_3 = 2*idx3;
        float fRe3 = data[N4 + n1_3] - data[N4 - 1 - n1_3];
        float fIm3 = data[N4 + n2_3] + data[N + N4 - 1 - n2_3];
        float r3 = fRe3 * cosT[idx3] + fIm3 * sinT[idx3];
        float i3 = fIm3 * cosT[idx3] - fRe3 * sinT[idx3];

        /* idx4 */
        int n1_4 = N2 - 1 - 2*idx4;
        int n2_4 = 2*idx4;
        float fRe4 = data[N4 + n1_4] - data[N4 - 1 - n1_4];
        float fIm4 = data[N4 + n2_4] + data[N + N4 - 1 - n2_4];
        float r4 = fRe4 * cosT[idx4] + fIm4 * sinT[idx4];
        float i4 = fIm4 * cosT[idx4] - fRe4 * sinT[idx4];

        /* Perform Radix-4 DIF Stage 0 butterfly */
        float t1 = r1 + r3, t2 = i1 + i3;
        float t3 = r2 + r4, t4 = i2 + i4;
        float t5 = r1 - r3, t6 = i1 - i3;
        float t7 = r2 - r4, t8 = i2 - i4;

        if (j == 0) {
            xr[idx1] = t1 + t3; xi[idx1] = t2 + t4;
            xr[idx3] = t5 + t8; xi[idx3] = t6 - t7;
            xr[idx2] = t1 - t3; xi[idx2] = t2 - t4;
            xr[idx4] = t5 - t8; xi[idx4] = t6 + t7;
        } else {
            int tw_idx = j;
            const float c1 = (float)costbl[tw_idx];
            const float s1 = (float)sintbl[tw_idx];
            const float c2 = (float)costbl[2 * tw_idx];
            const float s2 = (float)sintbl[2 * tw_idx];
            const float c3 = (float)costbl[3 * tw_idx];
            const float s3 = (float)sintbl[3 * tw_idx];

            xr[idx1] = t1 + t3;
            xi[idx1] = t2 + t4;

            float r1_out = t1 - t3; float i1_out = t2 - t4;
            float r2_out = t5 + t8; float i2_out = t6 - t7;
            float r3_out = t5 - t8; float i3_out = t6 + t7;

            xr[idx3] = r2_out * c1 - i2_out * s1;
            xi[idx3] = r2_out * s1 + i2_out * c1;
            xr[idx2] = r1_out * c2 - i1_out * s2;
            xi[idx2] = r1_out * s2 + i1_out * c2;
            xr[idx4] = r3_out * c3 - i3_out * s3;
            xi[idx4] = r3_out * s3 + i3_out * c3;
        }
    }
}

#if defined(__GNUC__)
__attribute__((cold, noinline))
#endif
void FilterBankInit(faacEncStruct* hEncoder)
{
    unsigned int channel;
    WindowPair longPair, shortPair;

    for (channel = 0; channel < hEncoder->numChannels; channel++) {
        hEncoder->freqBuff[channel] = (float*)AllocMemory(2*FRAME_LEN*sizeof(float));
        if (!hEncoder->freqBuff[channel]) return;
    }

    hEncoder->sin_window_long = (float*)AllocMemory(BLOCK_LEN_LONG*sizeof(float));
    hEncoder->sin_window_short = (float*)AllocMemory(BLOCK_LEN_SHORT*sizeof(float));
    hEncoder->kbd_window_long = (float*)AllocMemory(BLOCK_LEN_LONG*sizeof(float));
    hEncoder->kbd_window_short = (float*)AllocMemory(BLOCK_LEN_SHORT*sizeof(float));

    if (!hEncoder->sin_window_long || !hEncoder->sin_window_short ||
        !hEncoder->kbd_window_long || !hEncoder->kbd_window_short)
        return;

    longPair.sine = hEncoder->sin_window_long;
    longPair.kbd = hEncoder->kbd_window_long;
    shortPair.sine = hEncoder->sin_window_short;
    shortPair.kbd = hEncoder->kbd_window_short;

    BuildWindowPair(&longPair, BLOCK_LEN_LONG, 4.0);
    BuildWindowPair(&shortPair, BLOCK_LEN_SHORT, 6.0);

    hEncoder->gpsyInfo.sharedWorkBuffLong = (float*)AllocMemory(2*BLOCK_LEN_LONG*sizeof(float));
}

#if defined(__GNUC__)
__attribute__((cold, noinline))
#endif
void FilterBankEnd(faacEncStruct* hEncoder)
{
    unsigned int channel;

    for (channel = 0; channel < hEncoder->numChannels; channel++) {
        if (hEncoder->freqBuff[channel]) FreeMemory(hEncoder->freqBuff[channel]);
    }

    if (hEncoder->sin_window_long) FreeMemory(hEncoder->sin_window_long);
    if (hEncoder->sin_window_short) FreeMemory(hEncoder->sin_window_short);
    if (hEncoder->kbd_window_long) FreeMemory(hEncoder->kbd_window_long);
    if (hEncoder->kbd_window_short) FreeMemory(hEncoder->kbd_window_short);

    if (hEncoder->gpsyInfo.sharedWorkBuffLong) FreeMemory(hEncoder->gpsyInfo.sharedWorkBuffLong);
}

/* Four ICS window sequences, ISO/IEC 13818-7 4.3.2.4. */

typedef struct {
    float *dst;
    const float *src;
    const float *win;
    int len;
    bool reverse;
} WindowSeg;

static void ApplyWindowSeg(const WindowSeg *seg)
{
    int i;

    if (seg->reverse) {
        for (i = 0; i < seg->len; i++)
            seg->dst[i] = seg->src[i] * seg->win[seg->len - 1 - i];
    } else {
        for (i = 0; i < seg->len; i++)
            seg->dst[i] = seg->src[i] * seg->win[i];
    }
}

static void CopyFlat(float *dst, const float *src, int len)
{
    memcpy(dst, src, len * sizeof(float));
}

static void ZeroFlat(float *dst, int len)
{
    SetMemory(dst, 0, len * sizeof(float));
}

static const float *SelectWindow(faacEncStruct *hEncoder, int shape, bool isLong)
{
    if (shape == KBD_WINDOW)
        return isLong ? hEncoder->kbd_window_long : hEncoder->kbd_window_short;
    return isLong ? hEncoder->sin_window_long : hEncoder->sin_window_short;
}

void FilterBank(faacEncStruct* hEncoder,
                CoderInfo *coderInfo,
                float * restrict p_prev_data,
                float * restrict p_in_data,
                float * restrict p_out_mdct)
{
    float * restrict overlapBuf = hEncoder->gpsyInfo.sharedWorkBuffLong;
    int block_type = coderInfo->block_type;
    const float *leftWin, *rightWin;
    int k;

    /* Assemble the 2048-sample overlap window from the previous and
       current frame's time-domain samples. */
    memcpy(overlapBuf, p_prev_data, BLOCK_LEN_LONG*sizeof(float));
    memcpy(overlapBuf+BLOCK_LEN_LONG, p_in_data, BLOCK_LEN_LONG*sizeof(float));

    /* isLong is a literal per case below, not carried in from before the
       switch, so SelectWindow's dispatch folds to a single compare. */
    switch (block_type) {
    case ONLY_LONG_WINDOW: {
        WindowSeg left  = { p_out_mdct, overlapBuf,
                             SelectWindow(hEncoder, coderInfo->prev_window_shape, true), BLOCK_LEN_LONG, false };
        WindowSeg right = { p_out_mdct+BLOCK_LEN_LONG, overlapBuf+BLOCK_LEN_LONG,
                             SelectWindow(hEncoder, coderInfo->window_shape, true), BLOCK_LEN_LONG, true };

        ApplyWindowSeg(&left);
        ApplyWindowSeg(&right);
        MDCT(&hEncoder->fft_tables, p_out_mdct, 2*BLOCK_LEN_LONG, hEncoder->gpsyInfo.sharedWorkBuffLong);
        break;
    }

    case LONG_SHORT_WINDOW: {
        WindowSeg left  = { p_out_mdct, overlapBuf,
                             SelectWindow(hEncoder, coderInfo->prev_window_shape, true), BLOCK_LEN_LONG, false };
        WindowSeg right = { p_out_mdct+BLOCK_LEN_LONG+NFLAT_LS, overlapBuf+BLOCK_LEN_LONG+NFLAT_LS,
                             SelectWindow(hEncoder, coderInfo->window_shape, false), BLOCK_LEN_SHORT, true };

        ApplyWindowSeg(&left);
        CopyFlat(p_out_mdct+BLOCK_LEN_LONG, overlapBuf+BLOCK_LEN_LONG, NFLAT_LS);
        ApplyWindowSeg(&right);
        ZeroFlat(p_out_mdct+BLOCK_LEN_LONG+NFLAT_LS+BLOCK_LEN_SHORT, NFLAT_LS);
        MDCT(&hEncoder->fft_tables, p_out_mdct, 2*BLOCK_LEN_LONG, hEncoder->gpsyInfo.sharedWorkBuffLong);
        break;
    }

    case SHORT_LONG_WINDOW: {
        WindowSeg left  = { p_out_mdct+NFLAT_LS, overlapBuf+NFLAT_LS,
                             SelectWindow(hEncoder, coderInfo->prev_window_shape, false), BLOCK_LEN_SHORT, false };
        WindowSeg right = { p_out_mdct+BLOCK_LEN_LONG, overlapBuf+BLOCK_LEN_LONG,
                             SelectWindow(hEncoder, coderInfo->window_shape, true), BLOCK_LEN_LONG, true };

        ZeroFlat(p_out_mdct, NFLAT_LS);
        ApplyWindowSeg(&left);
        CopyFlat(p_out_mdct+NFLAT_LS+BLOCK_LEN_SHORT, overlapBuf+NFLAT_LS+BLOCK_LEN_SHORT, NFLAT_LS);
        ApplyWindowSeg(&right);
        MDCT(&hEncoder->fft_tables, p_out_mdct, 2*BLOCK_LEN_LONG, hEncoder->gpsyInfo.sharedWorkBuffLong);
        break;
    }

    case ONLY_SHORT_WINDOW: {
        float *src = overlapBuf + NFLAT_LS;
        float *dst = p_out_mdct;

        leftWin  = SelectWindow(hEncoder, coderInfo->prev_window_shape, false);
        rightWin = SelectWindow(hEncoder, coderInfo->window_shape, false);

        for (k = 0; k < MAX_SHORT_WINDOWS; k++) {
            WindowSeg left  = { dst, src, leftWin, BLOCK_LEN_SHORT, false };
            WindowSeg right = { dst+BLOCK_LEN_SHORT, src+BLOCK_LEN_SHORT, rightWin, BLOCK_LEN_SHORT, true };

            ApplyWindowSeg(&left);
            ApplyWindowSeg(&right);
            MDCT(&hEncoder->fft_tables, dst, 2*BLOCK_LEN_SHORT, hEncoder->gpsyInfo.sharedWorkBuffLong);

            dst += BLOCK_LEN_SHORT;
            src += BLOCK_LEN_SHORT;
            leftWin = rightWin;
        }
        break;
    }
    }
}

/* Loop-fused, bit-reversal-free MDCT engine.
 * Normative Reference: ISO/IEC 14496-3 Section 4.6.4.
 * Fuses time-domain folding and pre-twiddling directly with Stage 0 DIF FFT
 * to keep data in L1 cache, and merges post-twiddle unfolding with
 * bit-reversed lookups to eliminate physical permutation passes.
 * Deriving logm from N inside MDCT_run prevents compiler body-cloning across
 * block sizes in standard C11.
 */
#if defined(__GNUC__)
__attribute__((noinline, noclone))
#endif
static void MDCT_run(
    FFT_Tables *fft_tables,
    float * restrict data,
    int N,
    float * restrict work)
{
    const int logm = (N == 2 * BLOCK_LEN_LONG) ? LOGM_LONG : LOGM_SHORT;
    const int N2 = N >> 1;
    const int N4 = N >> 2;

    assert(fft_tables->mdct_cos[logm] != NULL);
    assert(fft_tables->mdct_sin[logm] != NULL);
    assert(fft_tables->costbl[logm] != NULL);
    assert(fft_tables->negsintbl[logm] != NULL);
    assert(fft_tables->reordertbl[logm] != NULL);

    const fftfloat * restrict cosT = fft_tables->mdct_cos[logm];
    const fftfloat * restrict sinT = fft_tables->mdct_sin[logm];

    float * restrict xr = work;
    float * restrict xi = work + N4;

    /* 1. Fuse pre-twiddle folding directly into Stage 0 DIF FFT */
    fused_pretwiddle_stage0(data, N, xr, xi, cosT, sinT, fft_tables->costbl[logm], fft_tables->negsintbl[logm]);

    /* 2. Run remaining stages of DIF FFT (starting from k = 1) */
    radix4_dif_run(xr, xi, logm, 1, fft_tables->costbl[logm], fft_tables->negsintbl[logm]);

    /* 3. Fuse bit-reversal reordering pass directly with the post-twiddle unfolding
     * step (Option A). This delivers natural-ordered spectral data downstream.
     */
    const unsigned short * restrict reorder = fft_tables->reordertbl[logm];
    int i;
    for (i = 0; i < N4; i++) {
        int rev_i = (int)reorder[i];
        float r = xr[rev_i];
        float j = xi[rev_i];

        float unfoldRe = 2.0f * (r * cosT[i] + j * sinT[i]);
        float unfoldIm = 2.0f * (j * cosT[i] - r * sinT[i]);

        int n2_idx = 2 * i;
        data[n2_idx]             = -unfoldRe;
        data[N2 - 1 - n2_idx]    =  unfoldIm;
        data[N2 + n2_idx]        = -unfoldIm;
        data[N - 1 - n2_idx]     =  unfoldRe;
    }
}

void MDCT( FFT_Tables *fft_tables, float * restrict data, int N, float * restrict work )
{
    assert(fft_tables != NULL);
    assert(data != NULL);
    assert(work != NULL);
    assert(N == 2 * BLOCK_LEN_SHORT || N == 2 * BLOCK_LEN_LONG);

    MDCT_run(fft_tables, data, N, work);
}
