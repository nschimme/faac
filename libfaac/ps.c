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

#include <math.h>
#include "ps.h"
#include "ps_tables.h"
#include "sbr_internal.h"
#include "sbr_tables.h"
#include "bitstream.h"
#include "util.h"
#include "fft.h"
#define SBR_PS_BANDS PS_BANDS
#define PHASE_BANDS PS_PHASE_BANDS
#define PS_MODE 1
#define SBR_PS_ICC_LEVELS 8
#define SBR_PS_ICC_MAX_INDEX 7
#define SBR_EXT_ID_PS 2
#define PS_HUFF_ICC_NSYMS 15
#define PS_HUFF_ICC_OFFSET 7
static int put_huff(BitStream *bs, bool write, const SBRHuffEntry *table, int nsyms, int offset, int delta)
{
    int sym = clamp_int(delta + offset, 0, nsyms - 1);
    if (write) PutBit(bs, table[sym].code, table[sym].len);
    return table[sym].len;
}


/* ISO/IEC 14496-3 8.6.4.3: the low three QMF bands need finer stereo
 * resolution. Six slots of SBR analysis look-ahead centre the 13-tap bank. */
void PsHybridAnalyze(PsHybrid *state, const float in[2][3][44][2],
                     float energy[2][4][64], float cross[4][64],
                     float crossImag[4][64], int slots)
{
    static const float g8[7] = { .00746082949812f, .02270420949825f, .04546865930473f,
        .07266113929591f, .09885108575264f, .11793710567217f, .125f };
    static const float g2[7] = { 0, .01899487526049f, 0, -.07293139167538f,
        0, .30596630545168f, .5f };
    static const unsigned char map[10] = {1,0,0,1,2,3,4,5,6,7};
    if (!state->initialized) {
        for (int q = 0; q < 8; q++)
            for (int j = 0; j < 7; j++) {
                double phase = -2.0 * 3.14159265358979323846 * (q + .5) * (j - 6) / 8;
                state->coef[q][j][0] = (float)(g8[j] * cos(phase));
                state->coef[q][j][1] = (float)(g8[j] * sin(phase));
            }
        state->initialized = 1;
    }
    for (int n = 0; n < slots; n += FAAC_SBR_DECIMATION) {
        float out[2][10][2];
        for (int ch = 0; ch < 2; ch++) {
            float t[8][2];
            for (int q = 0; q < 8; q++) {
                float sr = state->coef[q][6][0] * in[ch][0][n+6][0];
                float si = state->coef[q][6][0] * in[ch][0][n+6][1];
                for (int j = 0; j < 6; j++) {
                    float fr = state->coef[q][j][0], fi = state->coef[q][j][1];
                    float ar = in[ch][0][n+j][0], ai = in[ch][0][n+j][1];
                    float br = in[ch][0][n+12-j][0], bi = in[ch][0][n+12-j][1];
                    sr += fr*(ar+br) - fi*(ai-bi);
                    si += fr*(ai+bi) + fi*(ar-br);
                }
                t[q][0] = sr; t[q][1] = si;
            }
            for (int i = 0; i < 2; i++) {
                out[ch][0][i] = t[6][i]; out[ch][1][i] = t[7][i];
                out[ch][2][i] = t[0][i]; out[ch][3][i] = t[1][i];
                out[ch][4][i] = t[2][i]+t[5][i]; out[ch][5][i] = t[3][i]+t[4][i];
            }
            for (int k = 1; k < 3; k++) {
                int reverse = k == 1;
                for (int i = 0; i < 2; i++) {
                    float c = g2[6]*in[ch][k][n+6][i], sum = 0;
                    for (int j = 1; j < 6; j += 2)
                        sum += g2[j]*(in[ch][k][n+j][i]+in[ch][k][n+12-j][i]);
                    out[ch][6+2*(k-1)+reverse][i] = c+sum;
                    out[ch][6+2*(k-1)+!reverse][i] = c-sum;
                }
            }
        }
        int e = n*4/slots;
        for (int k = 0; k < 10; k++) {
            int b = map[k];
            float lr=out[0][k][0], li=out[0][k][1], rr=out[1][k][0], ri=out[1][k][1];
            energy[0][e][b] += lr*lr+li*li; energy[1][e][b] += rr*rr+ri*ri;
            cross[e][b] += lr*rr+li*ri;
            crossImag[e][b] += (k<2 ? -1 : 1)*(li*rr-lr*ri);
        }
    }
}

/* Phase indices wrap in eighths of a turn, as do their frequency deltas. */
static int ps_phase(float real, float imag)
{
    return (int)lrintf(atan2f(imag, real) * (4.0f / 3.14159265358979323846f)) & 7;
}

/* Estimate stereo parameters in the decoder's hybrid-band layout. */
static void ps_analyze_frame(const float energy[2][64], const float crossReal[64],
                             const float crossImag[64], SbrFrameData *fd)
{
    float l = 0, r = 0, cross = 0;
    for (int k = 0; k < PS_BANDS; k++) {
        l += energy[0][k]; r += energy[1][k];
        cross += crossReal[k];
    }
    float rho = cross / (sqrtf(l * r) + SBR_ENERGY_FLOOR);
    float w = 0.5f + 0.5f * fmaxf(0, fminf(1, -rho));
    fd->enable_phase = 0;

    for (int b = 0; b < SBR_PS_BANDS; b++) {
        float eL = energy[0][b], eR = energy[1][b];
        float eLR = crossReal[b], eLRi = crossImag[b];

        /* IID: pick the level nearest in dB. 10*log10(eL/eR) == 10/log2(10) *
         * log2(eL/eR), with the fine normative IID levels. */
        float iid_db = 3.01029996f * log2f((eL + SBR_ENERGY_FLOOR) / (eR + SBR_ENERGY_FLOOR));
        int lvl = 0;
        while (lvl < 30 && iid_db >= 0.5f * (ps_iid_db_fine[lvl] + ps_iid_db_fine[lvl + 1]))
            lvl++;
        fd->iid[b] = lvl - 15;

        float icc = eLR / (sqrtf(eL * eR) + SBR_ENERGY_FLOOR);
        if (b < PHASE_BANDS) {
            float coherence = hypotf(eLR, eLRi) / (sqrtf(eL * eR) + SBR_ENERGY_FLOOR);
            fd->ipd[b] = ps_phase(eLR, eLRi);
            fd->opd[b] = ps_phase(w * eL + (1 - w) * eLR, (1 - w) * eLRi);
            /* Real ICC suffices for in-phase and antiphase carriers; phase
             * parameters are useful for coherent complex pairs. */
            if (eL + eR < 1e-6f * (l + r) || coherence < 0.7f ||
                fd->ipd[b] == 0 || fd->ipd[b] == 4) {
                fd->ipd[b] = fd->opd[b] = 0;
            } else {
                fd->enable_phase = 1;
                icc = coherence;
            }
        }
        icc = fmaxf(-1.0f, fminf(icc, 1.0f));
        int best_icc = 0;
        float best_err = 2.0f;
        for (int i = 0; i <= SBR_PS_ICC_MAX_INDEX; i++) {
            float err = fabsf(icc - ps_icc_invq[i]);
            if (err < best_err) {
                best_err = err;
                best_icc = i;
            }
        }
        fd->icc[b] = best_icc;

    }


    /* Signal ICC only when some band is actually decorrelated. Index 0 is
     * ICC = 1.0 (fully coherent), for which the decoder's default is identical
     * and the payload bits would be wasted. */
    fd->enable_icc = 0;
    for (int b = 0; b < SBR_PS_BANDS; b++) {
        if (fd->icc[b] > 0) {
            fd->enable_icc = 1;
            break;
        }
    }

}

void PsAnalyze(SBRInfo *sbr, struct SignalAnalysis *sa, SbrFrameData *fd)
{
    fd->ps_num_env = 2;
    float energy[2][64], crossReal[64], crossImag[64];
    SbrFrameData params = {0};
    fd->enable_icc = fd->enable_phase = 0;
    for (int e = 0; e < fd->ps_num_env; e++) {
        memset(energy, 0, sizeof(energy));
        memset(crossReal, 0, sizeof(crossReal));
        memset(crossImag, 0, sizeof(crossImag));
        for (int p = e * 4 / fd->ps_num_env; p < (e + 1) * 4 / fd->ps_num_env; p++)
            for (int k = 0; k < PS_BANDS; k++) {
                energy[0][k] += sa->psE[0][p][k];
                energy[1][k] += sa->psE[1][p][k];
                crossReal[k] += sa->psCross[p][k];
                crossImag[k] += sa->psCrossIm[p][k];
            }
        ps_analyze_frame(energy, crossReal, crossImag, &params);
        fd->enable_icc |= params.enable_icc;
        fd->enable_phase |= params.enable_phase;
        memcpy(e ? fd->ps_extra[e-1].iid : fd->iid, params.iid, sizeof(params.iid));
        memcpy(e ? fd->ps_extra[e-1].icc : fd->icc, params.icc, sizeof(params.icc));
        memcpy(e ? fd->ps_extra[e-1].ipd : fd->ipd, params.ipd, sizeof(params.ipd));
        memcpy(e ? fd->ps_extra[e-1].opd : fd->opd, params.opd, sizeof(params.opd));
    }
    for (int h = 0; h < sa->numEnvelopes; h++)
        for (int k = sbr->kx; k < sbr->k2; k++)
            sa->bandE[0][h][k] = 0.5f * (sa->bandE[0][h][k] + sa->bandE[1][h][k]);
}

/* Bias toward a single channel when a sum would cancel. Normalize the carrier
 * to mean stereo energy, including hard pans and exact antiphase material. */
void PsDownmix(float *left, const float *right, int n)
{
    double l = 0, r = 0, cross = 0;
    for (int i = 0; i < n; i++) {
        l += (double)left[i] * left[i];
        r += (double)right[i] * right[i];
        cross += (double)left[i] * right[i];
    }
    double rho = cross / (sqrt(l * r) + 1e-20);
    float w = 0.5f + 0.5f * (float)fmax(0.0, -rho);
    double energy = w * w * l + (1 - w) * (1 - w) * r + 2 * w * (1 - w) * cross;
    float gain = energy > 1e-20 ? (float)sqrt(0.5 * (l + r) / energy) : 1;
    for (int i = 0; i < n; i++) left[i] = gain * (w * left[i] + (1 - w) * right[i]);
}

void PsSpectralDownmix(PsCarrier *state, float *left, float *right, float *carrier, int n)
{
    float work[1024], lf[1024], rf[1024];
    if (!state->initialized) {
        for (int i = 0; i < 512; i++)
            state->window[i] = 0.5f - 0.5f * cosf(2.0f * 3.14159265358979323846f * i / 512);
        state->initialized = 1;
    }
    for (int offset = 0; offset < n; offset += 256) {
        float *inputs[2] = {left + offset, right + offset};
        float *spectra[2] = {lf, rf};
        for (int ch = 0; ch < 2; ch++) {
            for (int i = 0; i < 256; i++) {
                work[i] = state->history[ch][i] * state->window[i];
                work[i+256] = inputs[ch][i] * state->window[i+256];
                float sample = inputs[ch][i];
                inputs[ch][i] = state->history[ch][i];
                state->history[ch][i] = sample;
            }
            memset(work + 512, 0, 512 * sizeof(float));
            fft(work, spectra[ch], FFT_LOGM_LONG);
        }
        for (int k = 0; k < 512; k++) {
            float real = 0.5f * (lf[k] + rf[k]);
            float imag = 0.5f * (lf[k+512] + rf[k+512]);
            float target = sqrtf(0.5f * (lf[k]*lf[k] + rf[k]*rf[k] + lf[k+512]*lf[k+512] + rf[k+512]*rf[k+512]));
            float magnitude = hypotf(real, imag);
            if (magnitude < 1e-5f * target) {
                real = lf[k]; imag = lf[k+512];
                magnitude = hypotf(real, imag);
            }
            float gain = magnitude > 1e-20f ? target / magnitude : 0;
            work[k] = real * gain;
            work[k+512] = -imag * gain;
        }
        fft(work, lf, FFT_LOGM_LONG);
        for (int i = 0; i < 256; i++) {
            float w0 = state->window[i], w1 = state->window[i+256];
            carrier[offset+i] = (state->overlap[i] + lf[i] * (w0 / 512)) / (w0*w0 + w1*w1);
            state->overlap[i] = lf[i+256] * (w1 / 512);
        }
    }
}

static int ps_param_bits(BitStream *bs, bool write, const int *cur,
                         const int *prev, int bands, int wrap,
                         const SBRHuffEntry *table, int nsyms, int offset)
{
    int bits = 0, ref = 0;
    for (int b = 0; b < bands; b++) {
        int delta = cur[b] - (prev ? prev[b] : ref);
        if (wrap) delta &= 7;
        bits += put_huff(bs, write, table, nsyms, offset, delta);
        ref = cur[b];
    }
    return bits;
}

/* Choose the cheaper representation without changing the stereo parameters.
 * The first envelope is self-contained; later envelopes may reference it. */
static int write_ps_params(BitStream *bs, bool write, const int *cur,
                           const int *prev, int bands, int wrap,
                           const SBRHuffEntry *df, const SBRHuffEntry *dt,
                           int nsyms, int offset)
{
    int freq = ps_param_bits(NULL, false, cur, NULL, bands, wrap, df, nsyms, offset);
    int time = prev ? ps_param_bits(NULL, false, cur, prev, bands, wrap, dt, nsyms, offset) : freq;
    bool temporal = prev && time < freq;
    if (write) PutBit(bs, temporal, 1);
    return 1 + ps_param_bits(bs, write, cur, temporal ? prev : NULL,
                             bands, wrap, temporal ? dt : df, nsyms, offset);
}

/* Nested PS extension size excludes its own size field. */
static int ps_write_phase(const SbrFrameData *fd, BitStream *bs, bool write)
{
    int bits = 0;
    for (int pass = 0; pass < (write ? 2 : 1); pass++) {
        bool emit = pass == 1;
        int n = 0;
#define WB(v,len) do { if (emit) PutBit(bs, (v), (len)); n += (len); } while (0)
        WB(0, 2); /* ps_extension_id = IPD/OPD */
        WB(fd->enable_phase, 1);
        if (fd->enable_phase) {
            for (int e = 0; e < (fd->ps_num_env ? fd->ps_num_env : 1); e++) {
                const int *params[2] = {
                    e ? fd->ps_extra[e - 1].ipd : fd->ipd,
                    e ? fd->ps_extra[e - 1].opd : fd->opd
                };
                const int *prev[2] = { e ? fd->ipd : NULL, e ? fd->opd : NULL };
                const SBRHuffEntry *df[2] = { ps_huff_ipd_df, ps_huff_opd_df };
                const SBRHuffEntry *dt[2] = { ps_huff_ipd_dt, ps_huff_opd_dt };
                for (int p = 0; p < 2; p++)
                    n += write_ps_params(bs, emit, params[p], prev[p], PHASE_BANDS,
                                         1, df[p], dt[p], 8, 0);
            }
        }
        WB(0, 1); /* reserved */
        WB(0, (8 - (n & 7)) & 7);
#undef WB
        if (pass == 0) {
            bits = (n / 8 >= 15 ? 12 : 4) + n;
            if (write) {
                PutBit(bs, n / 8 >= 15 ? 15 : n / 8, 4);
                if (n / 8 >= 15) PutBit(bs, n / 8 - 15, 8);
            }
        }
    }
    return bits;
}

int PsWrite(const SbrFrameData *fd, BitStream *bs, bool write)
{
    int bits = 0;

    /* Pass 0 sizes the payload, pass 1 emits it -- and is skipped entirely when
     * the caller only wants a bit count. */
    int ps_bits = 0;
    for (int pass = 0; pass < (write ? 2 : 1); pass++) {
        bool emit = (pass == 1);
        int n = 0;
#define PS_WB(v,len) do { if (emit) PutBit(bs,(v),(len)); n += (len); } while(0)
        PS_WB(SBR_EXT_ID_PS, 2);        /* bs_extension_id */
        PS_WB(1, 1);                    /* enable_ps_header: modes follow */
        PS_WB(1, 1);                    /* enable_iid */
        PS_WB(PS_MODE + 3, 3);
        PS_WB(fd->enable_icc, 1);       /* enable_icc */
        if (fd->enable_icc)
            PS_WB(PS_MODE, 3);
        PS_WB(1, 1);                    /* enable_ext: explicit phase state */
        PS_WB(0, 1);                    /* bs_frame_class = 0 (fixed borders) */
        /* Index 0 would mean no envelope, i.e. hold the previous image. */
        int n_env = fd->ps_num_env ? fd->ps_num_env : 1;
        PS_WB(n_env == 2 ? 2 : 1, 2);

        for (int e = 0; e < n_env; e++)
            n += write_ps_params(emit ? bs : NULL, emit,
                                 e ? fd->ps_extra[e - 1].iid : fd->iid,
                                 e ? fd->iid : NULL, SBR_PS_BANDS, 0,
                                 ps_huff_iid_df_fine, ps_huff_iid_dt_fine,
                                 PS_HUFF_IID_DF_FINE_NSYMS, PS_HUFF_IID_DF_FINE_OFFSET);
        if (fd->enable_icc)
            for (int e = 0; e < n_env; e++)
                n += write_ps_params(emit ? bs : NULL, emit,
                                     e ? fd->ps_extra[e - 1].icc : fd->icc,
                                     e ? fd->icc : NULL, SBR_PS_BANDS, 0,
                                     ps_huff_icc_df, ps_huff_icc_dt,
                                     PS_HUFF_ICC_NSYMS, PS_HUFF_ICC_OFFSET);

        n += ps_write_phase(fd, bs, emit);

        /* bs_extension_size counts whole bytes from bs_extension_id onward, so
         * the payload is padded out to a byte boundary. */
        PS_WB(0, (8 - (n & 7)) & 7);    /* bs_fill_bits */
#undef PS_WB

        if (pass == 0) {
            ps_bits = n;
            int ps_bytes = ps_bits / 8;
            if (ps_bytes < 15) {
                if (write) PutBit(bs, ps_bytes, 4);
                bits += 4;
            } else {
                if (write) { PutBit(bs, 15, 4); PutBit(bs, ps_bytes - 15, 8); }
                bits += 12;
            }
        }
    }
    bits += ps_bits;

    return bits;
}
