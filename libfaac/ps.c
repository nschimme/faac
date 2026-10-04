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
#define SBR_PS_BANDS PS_BANDS
#define SBR_PS_IID_LEVELS 15
#define SBR_PS_ICC_LEVELS 8
#define SBR_PS_ICC_MAX_INDEX 7
#define SBR_EXT_ID_PS 2
#define PS_HUFF_IID_NSYMS 29
#define PS_HUFF_IID_OFFSET 14
#define PS_HUFF_ICC_NSYMS 15
#define PS_HUFF_ICC_OFFSET 7
static int put_huff(BitStream *bs, bool write, const SBRHuffEntry *table, int nsyms, int offset, int delta)
{
    int sym = clamp_int(delta + offset, 0, nsyms - 1);
    if (write) PutBit(bs, table[sym].code, table[sym].len);
    return table[sym].len;
}


static const unsigned char ps_band_qmf[SBR_PS_BANDS][2] = {
    { 0,  1}, { 0,  1}, { 1,  2}, { 2,  3}, { 3,  5},
    { 5,  7}, { 7,  9}, { 9, 14}, {14, 23}, {23, 64}
};

/* Phase indices wrap in eighths of a turn, as do their frequency deltas. */
static int ps_phase(float real, float imag)
{
    return (int)lrintf(atan2f(imag, real) * (4.0f / 3.14159265358979323846f)) & 7;
}

/* Coarse IID/ICC and optional low-band IPD/OPD share the SBR payload delay. */
void PsAnalyze(SBRInfo *sbr, struct SignalAnalysis *sa, SbrFrameData *fd)
{
    int n_env = sa->numEnvelopes;
    float l = 0, r = 0, cross = 0;
    for (int h = 0; h < n_env; h++)
        for (int k = 0; k < 64; k++) {
            l += sa->bandE[0][h][k]; r += sa->bandE[1][h][k];
            cross += sa->bandCrossE[h][k];
        }
    float rho = cross / (sqrtf(l * r) + SBR_ENERGY_FLOOR);
    float w = 0.5f + 0.5f * fmaxf(0, fminf(1, -rho));
    fd->enable_phase = 0;

    for (int b = 0; b < SBR_PS_BANDS; b++) {
        float eL = 0.0f, eR = 0.0f, eLR = 0.0f, eLRi = 0.0f;

        for (int h = 0; h < n_env; h++) {
            for (int k = ps_band_qmf[b][0]; k < ps_band_qmf[b][1]; k++) {
                eL  += sa->bandE[0][h][k];
                eR  += sa->bandE[1][h][k];
                eLR += sa->bandCrossE[h][k];
                eLRi += sa->bandCrossIm[h][k];
            }
        }

        /* IID: pick the level nearest in dB. 10*log10(eL/eR) == 10/log2(10) *
         * log2(eL/eR), with the default normative IID levels. */
        float iid_db = 3.01029996f * log2f((eL + SBR_ENERGY_FLOOR) / (eR + SBR_ENERGY_FLOOR));
        int lvl = 0;
        while (lvl < SBR_PS_IID_LEVELS - 1 && iid_db >= 0.5f * (ps_iid_db_default[lvl] + ps_iid_db_default[lvl + 1]))
            lvl++;
        fd->iid[b] = lvl - (SBR_PS_IID_LEVELS / 2);

        float icc = eLR / (sqrtf(eL * eR) + SBR_ENERGY_FLOOR);
        if (b < PS_PHASE_BANDS) {
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

    /* SBR supplies mean stereo energy; the PS matrix splits it into L/R. */
    for (int h = 0; h < n_env; h++)
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

static int write_ps_params(BitStream *bs, bool write, const int *cur,
                           const SBRHuffEntry *table, int nsyms, int offset)
{
    int bits = 0;
    int ref = 0;

    for (int b = 0; b < SBR_PS_BANDS; b++) {
        bits += put_huff(bs, write, table, nsyms, offset, cur[b] - ref);
        ref = cur[b];
    }
    return bits;
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
            const int *params[2] = { fd->ipd, fd->opd };
            const SBRHuffEntry *books[2] = { ps_huff_ipd_df, ps_huff_opd_df };
            for (int p = 0; p < 2; p++) {
                WB(0, 1); /* frequency delta */
                int ref = 0;
                for (int b = 0; b < PS_PHASE_BANDS; b++) {
                    n += put_huff(bs, emit, books[p], 8, 0, (params[p][b] - ref) & 7);
                    ref = params[p][b];
                }
            }
        }
        WB(0, 1); /* reserved */
        WB(0, (8 - (n & 7)) & 7);
#undef WB
        if (pass == 0) {
            bits = 4 + n;
            if (write) PutBit(bs, n / 8, 4);
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
        PS_WB(0, 3);                    /* iid_mode = 0 (10 bands, default res) */
        PS_WB(fd->enable_icc, 1);       /* enable_icc */
        if (fd->enable_icc)
            PS_WB(0, 3);                /* icc_mode = 0 (10 bands) */
        PS_WB(1, 1);                    /* enable_ext: explicit phase state */
        PS_WB(0, 1);                    /* bs_frame_class = 0 (fixed borders) */
        /* num_env_tab[0][1] == 1: one envelope for the whole frame. Index 0
         * would mean *zero* envelopes, i.e. "hold the previous frame's image". */
        PS_WB(1, 2);                    /* bs_num_env_idx */

        PS_WB(0, 1);                    /* bs_iid_dt = 0 (frequency delta) */
        n += write_ps_params(emit ? bs : NULL, emit, fd->iid,
                             ps_huff_iid_df, PS_HUFF_IID_NSYMS, PS_HUFF_IID_OFFSET);

        if (fd->enable_icc) {
            PS_WB(0, 1);                /* bs_icc_dt = 0 (frequency delta) */
            n += write_ps_params(emit ? bs : NULL, emit, fd->icc,
                                 ps_huff_icc_df, PS_HUFF_ICC_NSYMS, PS_HUFF_ICC_OFFSET);
        }

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
