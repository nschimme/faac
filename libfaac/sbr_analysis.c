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

#include "sbr.h"
#include "sbr_analysis.h"
#include "sbr_internal.h"
#include "util.h"
#include <string.h>

/* Which envelope a QMF slot falls in; slots before tEnv[0] fold into
 * envelope 0 rather than dropping their energy. */
static inline int sbr_env_of_slot(int numEnvelopes, const int *envStart, int slot)
{
    int e = 0;
    while (e + 1 < numEnvelopes && slot >= envStart[e + 1]) e++;
    return e;
}

/* Second-order predictor from a band's autocorrelation R(0..2) (ISO 14496-3
 * §4.6.18.6.2, what the decoder's inverse filter is built from) and its
 * tonality: the energy the predictor explains over what it leaves. */
static float sbr_tonality(float (*R)[2], float a0[2], float a1[2])
{
    float r0 = R[0][0];
    float r1r = R[1][0], r1i = R[1][1], r2r = R[2][0], r2i = R[2][1];
    float det = r0 * r0 - (r1r * r1r + r1i * r1i) * (1.0f / 1.000001f);
    float a1r = 0, a1i = 0, a0r = 0, a0i = 0;
    if (det != 0.0f) {
        /* a1 = (R1^2 - R2 R0) / (R0^2 - |R1|^2) */
        float inv = 1.0f / det;
        a1r = (r1r * r1r - r1i * r1i - r2r * r0) * inv;
        a1i = (2.0f * r1r * r1i - r2i * r0) * inv;
    }
    if (r0 != 0.0f) {
        /* a0 = -(R1 + a1 conj(R1)) / R0 */
        float inv = 1.0f / r0;
        a0r = -(r1r + a1r * r1r + a1i * r1i) * inv;
        a0i = -(r1i + a1i * r1r - a1r * r1i) * inv;
    }
    if (a0r * a0r + a0i * a0i >= 16.0f || a1r * a1r + a1i * a1i >= 16.0f)
        a0r = a0i = a1r = a1i = 0.0f;
    a0[0] = a0r; a0[1] = a0i;
    a1[0] = a1r; a1[1] = a1i;

    /* residual |x + a0 x1 + a1 x2|^2 = R0 (1 + |a0|^2 + |a1|^2)
     *   + 2 Re(conj(a0) R1) + 2 Re(conj(a1) R2) + 2 Re(conj(a0) a1 conj(R1)) */
    float e = r0 * (1.0f + a0r * a0r + a0i * a0i + a1r * a1r + a1i * a1i)
            + 2.0f * (a0r * r1r + a0i * r1i)
            + 2.0f * (a1r * r2r + a1i * r2i)
            + 2.0f * ((a0r * a1r + a0i * a1i) * r1r + (a0r * a1i - a0i * a1r) * r1i);
    if (e < 0.0f) e = 0.0f;
    return (r0 > e) ? (r0 - e) / (e + SBR_ENERGY_FLOOR) : 0.0f;
}

/* R(d) of a band filtered by 1 + c0 z^-1 + c1 z^-2, from the band's own
 * R(0..4): sum over the filter taps j, k of c_j conj(c_k) R(d + k - j),
 * with R(-m) = conj(R(m)). */
static void sbr_filtered_acf(float (*R)[2], const float c0[2], const float c1[2], float Ry[3][2])
{
    float c0r = c0[0], c0i = c0[1], c1r = c1[0], c1i = c1[1];
    float m0 = c0r * c0r + c0i * c0i, m1 = c1r * c1r + c1i * c1i;
    /* c0 conj(c1) */
    float xr = c0r * c1r + c0i * c1i, xi = c0i * c1r - c0r * c1i;
    for (int d = 0; d < 3; d++) {
        /* (j,k) = (0,0), (1,1), (2,2): (1 + |c0|^2 + |c1|^2) R(d) */
        float s = 1.0f + m0 + m1;
        float sr = s * R[d][0], si = s * R[d][1];
        /* (0,1): conj(c0) R(d+1); (1,0): c0 R(d-1) */
        const float *p = R[d + 1];
        sr += c0r * p[0] + c0i * p[1]; si += c0r * p[1] - c0i * p[0];
        float qr = R[d ? d - 1 : 1][0], qi = d ? R[d - 1][1] : -R[1][1];
        sr += c0r * qr - c0i * qi; si += c0r * qi + c0i * qr;
        /* (0,2): conj(c1) R(d+2); (2,0): c1 R(d-2) */
        p = R[d + 2];
        sr += c1r * p[0] + c1i * p[1]; si += c1r * p[1] - c1i * p[0];
        qr = R[d < 2 ? 2 - d : d - 2][0]; qi = d < 2 ? -R[2 - d][1] : R[d - 2][1];
        sr += c1r * qr - c1i * qi; si += c1r * qi + c1i * qr;
        /* (1,2): c0 conj(c1) R(d+1); (2,1): conj(c0) c1 R(d-1) */
        p = R[d + 1];
        sr += xr * p[0] - xi * p[1]; si += xr * p[1] + xi * p[0];
        qr = R[d ? d - 1 : 1][0]; qi = d ? R[d - 1][1] : -R[1][1];
        sr += xr * qr + xi * qi; si += xr * qi - xi * qr;
        Ry[d][0] = sr; Ry[d][1] = si;
    }
}

/* Tonality of every SBR band, and of every patch source band as the decoder
 * will hear it at the strongest chirp factor: inverse filtered
 * (§4.6.18.6.1) and measured again, both from the band's autocorrelation
 * over every SBR_TON_STEP-th slot of the frame: a tone stays a tone and
 * noise stays noise when a band is decimated. The lag sums run across
 * bands, plane by plane. */
static void sbr_band_tonality(SignalAnalysis *sa, int ch, float sub[][2][SBR_QMF_BANDS_64], int kLo, int kx, int kEnd)
{
    /* Lags 3 and 4 only feed the whitened view of the source bands. */
    float R[5][2][SBR_QMF_BANDS_64];
    memset(R, 0, sizeof(R));
    for (int i = 2 * SBR_TON_STEP; i < SBR_TON_SLOTS; i += SBR_TON_STEP) {
        const float * restrict xr = sub[i][0], * restrict xi = sub[i][1];
        for (int d = 0; d < 5; d++) {
            if (d == 3 && i < 4 * SBR_TON_STEP) break;
            float * restrict ar = R[d][0], * restrict ai = R[d][1];
            const float * restrict yr = sub[i - SBR_TON_STEP * d][0], * restrict yi = sub[i - SBR_TON_STEP * d][1];
            int kHi = d < 3 ? kEnd : kx;
            for (int k = kLo; k < kHi; k++) {
                ar[k] += xr[k] * yr[k] + xi[k] * yi[k];
                ai[k] += xi[k] * yr[k] - xr[k] * yi[k];
            }
        }
    }

    for (int k = kLo; k < kEnd; k++) {
        float Rk[5][2], Ry[3][2];
        float a0[2], a1[2], c0[2], c1[2], b0[2], b1[2];
        for (int d = 0; d < 5; d++) { Rk[d][0] = R[d][0][k]; Rk[d][1] = R[d][1][k]; }
        float t = sbr_tonality(Rk, a0, a1);
        if (k >= kx) {
            sa->tonTgt[ch][k] = t;
            continue;
        }
        c0[0] = a0[0] * SBR_CHIRP_MAX; c0[1] = a0[1] * SBR_CHIRP_MAX;
        c1[0] = a1[0] * SBR_CHIRP_MAX * SBR_CHIRP_MAX; c1[1] = a1[1] * SBR_CHIRP_MAX * SBR_CHIRP_MAX;
        sbr_filtered_acf(Rk, c0, c1, Ry);
        sa->tonSrc[ch][k][0] = t;
        sa->tonSrc[ch][k][1] = sbr_tonality(Ry, b0, b1);
    }
}

/* Multi-pass signal analysis: transient detection, temporal grid selection,
 * and subband energy accumulation. */
void SbrAnalyze(SignalAnalysis *sa, float *fullPtrs[], int nch, const bool *isLfe, int numSamples, struct SBRInfo *sbr)
{
    int num_slots = numSamples / SBR_QMF_BANDS_64;
    int sampled = (num_slots - 1) / FAAC_SBR_DECIMATION + 1;
    float workspace[SBR_QMF_OVL_LEN_64 + 2 * FRAME_LEN];

    sa->valid = 1;
    sa->numSlots = num_slots;
    sa->sampled = sampled;

    /* Pass 1: Time-domain transient detection. Identifies the temporal position
     * and strength of transients across all channels. */
    for (int ch = 0; ch < nch; ch++) {
        float smax = 0.0f, ssum = 0.0f;
        int smax_idx = 0;
        float slot_hp_eng[128]; /* high-pass energy per slot (max slots = 2*1024/64 = 32) */

        sa->ch[ch].wantShort = 0;
        float val_in = sa->ch[ch].lastVal;
        const float * restrict p_in = fullPtrs[ch];
        for (int slot = 0; slot < num_slots; slot++) {
            float stot = 0.0f;
            float hp_stot = 0.0f;
            for (int n = 0; n < SBR_QMF_BANDS_64; n += 4) {
                float v0 = p_in[0], v1 = p_in[1], v2 = p_in[2], v3 = p_in[3];
                stot += v0 * v0 + v1 * v1 + v2 * v2 + v3 * v3;
                float d0 = v0 - val_in, d1 = v1 - v0, d2 = v2 - v1, d3 = v3 - v2;
                hp_stot += d0 * d0 + d1 * d1 + d2 * d2 + d3 * d3;
                val_in = v3; p_in += 4;
            }
            if (slot < 128) slot_hp_eng[slot] = hp_stot;

            if (stot > smax) {
                smax = stot;
                smax_idx = slot;
            }
            ssum += stot;
        }
        sa->ch[ch].lastVal = val_in;

        sa->ch[ch].transientStrength = smax * (float)num_slots / (ssum + SBR_ENERGY_FLOOR);
        sa->ch[ch].transientSlot = smax_idx;

        /* Evaluate relative energy jumps to inform block switching. */
        float last_hp_eng = 0.0f;
        int have_last = 0;
        for (int slot = 0; slot < num_slots; slot++) {
            if (slot >= 128) break;
            float hp_eng = slot_hp_eng[slot];
            if (have_last) {
                float toteng = (hp_eng < last_hp_eng) ? hp_eng : last_hp_eng;
                float volchg = (hp_eng > last_hp_eng) ? (hp_eng - last_hp_eng) : (last_hp_eng - hp_eng);
                /* PSY_TD_THRESH = 0.5 */
                if (volchg > (0.5f * toteng)) {
                    sa->ch[ch].wantShort = 1;
                    break;
                }
            }
            last_hp_eng = hp_eng;
            have_last = 1;
        }
    }

    /* Choose the temporal grid based on the strongest transient. Synchronizes
     * envelope borders across all channels to maintain spatial imaging. The
     * LFE carries no SBR, so it gets no vote. */
    float frameStrength = 0.0f;
    int frameSlot = 0;
    for (int ch = 0; ch < nch; ch++) {
        if (isLfe[ch]) continue;
        if (sa->ch[ch].transientStrength > frameStrength) {
            frameStrength = sa->ch[ch].transientStrength;
            frameSlot = sa->ch[ch].transientSlot;
        }
    }

    if (frameStrength > SBR_TRANSIENT_THRESH_DEFAULT) {
        int Ts = (num_slots > 0) ? frameSlot * SBR_NUM_TIME_SLOTS / num_slots : 0; /* 0..16 */
        int rel = clamp_int((Ts - 2) / 2, 0, 3);
        int innerSbr = 2 * rel + 2;                  /* {2,4,6,8} */
        sa->numEnvelopes = 2;
        sa->frameClass = SBR_FRAME_CLASS_VARFIX;
        sa->tEnv[0] = 0;
        sa->tEnv[1] = innerSbr;
        sa->tEnv[2] = SBR_NUM_TIME_SLOTS;
        sa->bsPointer = 0;
    } else {
        sa->numEnvelopes = 1;
        sa->frameClass = SBR_FRAME_CLASS_FIXFIX;
        sa->tEnv[0] = 0;
        sa->tEnv[1] = SBR_NUM_TIME_SLOTS;
        sa->bsPointer = 0;
    }

    /* Envelope borders in QMF slots, for binning the per-slot energies below. */
    int envStart[SBR_MAX_ENVELOPES + 1];
    for (int e = 0; e <= sa->numEnvelopes; e++)
        envStart[e] = sa->tEnv[e] * num_slots / SBR_NUM_TIME_SLOTS;

    /* Count slots per envelope for power normalization. */
    for (int e = 0; e < sa->numEnvelopes; e++) sa->envSampled[e] = 0;
    for (int slot = 0; slot < num_slots; slot++) {
#if FAAC_SBR_DECIMATION > 1
        if (slot % FAAC_SBR_DECIMATION != 0) continue;
#endif
        sa->envSampled[sbr_env_of_slot(sa->numEnvelopes, envStart, slot)]++;
    }
    for (int e = 0; e < sa->numEnvelopes; e++)
        if (sa->envSampled[e] < 1) sa->envSampled[e] = 1;

    /* Pass 2: subband analysis. The envelope quantizer needs the band energy
     * per envelope over [kx, k2); the tonality below also needs the patch
     * source bands, which start at srcLo. */
    if (sbr) {
        int kLo = sbr->srcLo;
        int kx = sbr->kx;
        int kEnd = sbr->k2;
        for (int ch = 0; ch < nch; ch++) {
            if (isLfe[ch]) continue;
            memset(sa->bandE[ch], 0, sizeof(sa->bandE[ch]));

            memcpy(workspace, sbr->ch[ch].qmfOvl64, SBR_QMF_OVL_LEN_64 * sizeof(float));
            memcpy(workspace + SBR_QMF_OVL_LEN_64, fullPtrs[ch], numSamples * sizeof(float));

            float sub[SBR_TON_SLOTS][2][SBR_QMF_BANDS_64];
            int n = 0;
            for (int slot = 0; slot < num_slots; slot++) {
#if FAAC_SBR_DECIMATION > 1
                if (slot % FAAC_SBR_DECIMATION == 0)
#endif
                {
                    const float * restrict re = sub[n][0], * restrict im = sub[n][1];
                    SbrQmfAnalysis(sbr, workspace + slot * SBR_QMF_BANDS_64, sub[n][0], sub[n][1], kLo, kEnd);

                    int e = sbr_env_of_slot(sa->numEnvelopes, envStart, slot);

                    float * restrict bE = sa->bandE[ch][e];
                    for (int k = kx; k < kEnd; k++)
                        bE[k] += re[k] * re[k] + im[k] * im[k];
                    n++;
                }
            }
            sbr_band_tonality(sa, ch, sub, kLo, kx, kEnd);
        }
    }
}
