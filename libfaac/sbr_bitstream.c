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
#include <string.h>
#include <limits.h>

#include "sbr.h"
#include "sbr_internal.h"
#include "sbr_tables.h"
#include "bitstream.h"
#include "channels.h"
#include "util.h"
#include "faac_internal.h"

static int write_sbr_header(const SBRInfo *sbr, BitStream *bs, bool write)
{
    if (write) {
        /* ISO 14496-3:2009 §4.6.18.5 sbr_header() (21 bits) */
        PutBit(bs, SBR_AMP_RES,         1); /* bs_amp_res: 0=1.5dB, 1=3dB */
        PutBit(bs, sbr->bs_start_freq,  4); /* bs_start_freq: crossover index */
        PutBit(bs, sbr->bs_stop_freq,   4); /* bs_stop_freq: high-band ceil */
        PutBit(bs, sbr->bs_xover_band,  3); /* bs_xover_band: low-res split (0=none) */
        PutBit(bs, 0,                   2); /* bs_reserved */
        PutBit(bs, 1,                   1); /* bs_header_extra_1 = 1 */
        PutBit(bs, 0,                   1); /* bs_header_extra_2 = 0 */
        PutBit(bs, sbr->bs_freq_scale,  2);
        PutBit(bs, sbr->bs_alter_scale, 1);
        PutBit(bs, 0,                   2); /* bs_noise_bands = 0 */
    }
    return 21;
}

/* Width of the transient pointer field, indexed by number of envelopes. */
static const int sbr_ceil_log2[] = { 0, 1, 2, 2, 3, 3 };

static int write_sbr_grid(const SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs, bool write)
{
    int num_env = fd->numEnvelopes;
    int bits = 2;

    if (write) PutBit(bs, fd->frameClass, 2);
    if (fd->frameClass == SBR_FRAME_CLASS_VARFIX) {
        if (write) {
            PutBit(bs, fd->tEnv[0], 2);                 /* bs_var_bord_0 */
            PutBit(bs, num_env - 1, 2);                  /* bs_num_rel_0   */
            for (int i = 0; i < num_env - 1; i++)
                PutBit(bs, (fd->tEnv[i + 1] - fd->tEnv[i] - 2) / 2, 2); /* bs_rel_bord */
        }
        int ptr_len = sbr_ceil_log2[num_env];
        if (write) {
            PutBit(bs, fd->bsPointer, ptr_len);
            for (int i = 0; i < num_env; i++)
                PutBit(bs, sbr->bs_freq_res, 1);
        }
        bits += 4 + 2 * (num_env - 1) + ptr_len + num_env;
    } else {
        if (write) {
            PutBit(bs, num_env > 1 ? 1 : 0, 2);         /* bs_num_env = 1 << this */
            PutBit(bs, sbr->bs_freq_res, 1);
        }
        bits += 3;
    }
    return bits;
}

static int write_sbr_dtdf(const SBRChannel *sc, const SbrFrameData *fd, int noiseLinked, BitStream *bs, bool write)
{
    int n_q = fd->numEnvelopes > 1 ? 2 : 1;
    if (write) {
        for (int e = 0; e < fd->numEnvelopes; e++) PutBit(bs, (sc->envDt >> e) & 1, 1);
        for (int q = 0; q < n_q; q++) PutBit(bs, q || noiseLinked, 1);
    }
    return fd->numEnvelopes + n_q;
}

static int write_sbr_invf(BitStream *bs, bool write)
{
    if (write) PutBit(bs, SBR_INVF_MODE, 2);
    return 2;
}

struct EnvBooks {
    const SBRHuffEntry *f, *t;
    int lav, start;
};

static const struct EnvBooks books[2][2] = {
    { { f_huff_env_1_5dB, t_huff_env_1_5dB, F_HUFF_ENV_1_5DB_OFFSET, 7 },
      { f_huff_env_3_0dB, t_huff_env_3_0dB, F_HUFF_ENV_3_0DB_OFFSET, 6 } },
    { { f_huff_env_bal_1_5dB, t_huff_env_bal_1_5dB, F_HUFF_ENV_BAL_1_5DB_OFFSET, 6 },
      { f_huff_env_bal_3_0dB, t_huff_env_bal_3_0dB, F_HUFF_ENV_BAL_3_0DB_OFFSET, 5 } },
};

static int code_envelope(const SBRInfo *sbr, const SbrFrameData *fd, const int *cur, const int *ref,
                         int bal, BitAccumulator *acc, bool write)
{
    const struct EnvBooks *bk = &books[bal][fd->eff_amp_res];
    int nb = sbr_env_bands(sbr, fd);
    const SBRHuffEntry *tab = bk->f;
    int offset = bk->lav, bits = 0, b = 0;

    if (ref) {
        tab = bk->t;
        offset = T_HUFF_ENV_LAV;
    } else {
        bits = bk->start;
        int first_max = (1 << bits) - 1;
        if (write) AccumPutBits(acc, (uint32_t)clamp_int(cur[0], 0, first_max), bits);
        b = 1;
    }
    for (; b < nb; b++) {
        int d = cur[b] - (ref ? ref[b] : cur[b - 1]);
        if (d < -offset || d > offset) return INT_MAX;
        int idx = d + offset;
        if (write) AccumPutBits(acc, sbr_huff_code(tab[idx]), sbr_huff_len(tab[idx]));
        bits += sbr_huff_len(tab[idx]);
    }
    return bits;
}

static int write_sbr_envelope(SBRInfo *sbr, const SbrFrameData *fd, const int (*env)[SBR_MAX_BANDS],
                              int bal, int linked, BitStream *bs, int ch, bool write)
{
    SBRChannel *sc = &sbr->ch[ch];
    unsigned chosen = 0;
    int bits = 0;
    BitAccumulator acc = {0};

    if (write) AccumBegin(&acc, bs);
    for (int e = 0; e < fd->numEnvelopes; e++) {
        const int *ref = (e || linked) ? (e ? env[e - 1] : sc->ref[~sbr->frameCount & 1].env) : NULL;
        int first = write ? ((sc->envDt >> e) & 1) : 0;
        int last = write ? first : (ref != NULL);
        int best = INT_MAX;
        for (int t = first; t <= last; t++) {
            int n = code_envelope(sbr, fd, env[e], t ? ref : NULL, bal, &acc, write);
            if (n < best) { best = n; chosen = (chosen & ~(1u << e)) | ((unsigned)t << e); }
        }
        if (best == INT_MAX) { bits = INT_MAX; break; }
        bits += best;
    }
    if (write) AccumEnd(&acc);
    else sc->envDt = chosen;
    return bits;
}

static int write_sbr_noise(const SbrFrameData *fd, int value, int noiseLinked, BitStream *bs, bool write)
{
    int n_q = fd->numEnvelopes > 1 ? 2 : 1;
    int bits = 0;
    for (int q = 0; q < n_q; q++) {
        int len = (q || noiseLinked) ? 1 : 5;
        if (write) PutBit(bs, len == 1 ? 0 : value, len);
        bits += len;
    }
    return bits;
}

static void couple_envelopes(SBRInfo *sbr, const SbrFrameData *fd, int ch0)
{
    int amp = fd->eff_amp_res;
    int pan = amp ? 12 : 24;
    int nb = sbr_env_bands(sbr, fd);

    for (int e = 0; e < fd->numEnvelopes; e++) {
        const int *restrict left = fd->ch[ch0].envData[e], *restrict right = fd->ch[ch0 + 1].envData[e];
        int *restrict level = sbr->cplEnv[0][e], *restrict balance = sbr->cplEnv[1][e];
        for (int b = 0; b < nb; b++) {
            int l = left[b], r = right[b];
            int d = l > r ? l - r : r - l;
            level[b] = (l > r ? l : r) - (d >= 2) - (!amp && d >= 5);
            balance[b] = (clamp_int(pan + l - r, 0, 2 * pan) + 1) >> 1;
        }
    }
}

static int write_sbr_channels(SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs, int nch, int ch0,
                              int sendHeader, int coupled, bool write)
{
    int bits = (nch == 2) ? 5 : 3;
    const SBRChannel *sc0 = &sbr->ch[ch0];
    const SbrEnvRef *prev = &sc0->ref[~sbr->frameCount & 1];
    int noiseLinked = !sendHeader && prev->nb && prev->coupled == coupled;
    int linked = noiseLinked && prev->nb == sbr_env_bands(sbr, fd) && prev->ampRes == fd->eff_amp_res;
    const int (*env[2])[SBR_MAX_BANDS];
    int ngrid = coupled ? 1 : nch;

    for (int ch = 0; ch < nch; ch++)
        env[ch] = coupled ? (const int (*)[SBR_MAX_BANDS])sbr->cplEnv[ch]
                          : (const int (*)[SBR_MAX_BANDS])fd->ch[ch0 + ch].envData;

    if (write) PutBit(bs, coupled, nch);

    for (int ch = 0; ch < ngrid; ch++)
        bits += write_sbr_grid(sbr, fd, bs, write);
    for (int ch = 0; ch < nch; ch++)
        bits += write_sbr_dtdf(&sbr->ch[ch0 + ch], fd, noiseLinked, bs, write);
    for (int ch = 0; ch < ngrid; ch++)
        bits += write_sbr_invf(bs, write);

    for (int k = 0; k < 2 * nch; k++) {
        int ch = coupled ? k >> 1 : k % nch;
        if (coupled ? k & 1 : k >= nch) {
            bits += write_sbr_noise(fd, coupled && ch ? 6 : SBR_NOISE_LEVEL_DEFAULT, noiseLinked, bs, write);
        } else {
            int n = write_sbr_envelope(sbr, fd, env[ch], coupled && ch, linked, bs, ch0 + ch, write);
            if (n == INT_MAX) return INT_MAX;
            bits += n;
        }
    }

    if (write) PutBit(bs, 0, nch + 1);

    if (write) {
        int nb = sbr_env_bands(sbr, fd);
        for (int ch = 0; ch < nch; ch++) {
            SbrEnvRef *cur = &sbr->ch[ch0 + ch].ref[sbr->frameCount & 1];
            memcpy(cur->env, env[ch][fd->numEnvelopes - 1], nb * sizeof(int));
            cur->nb = nb;
            cur->ampRes = fd->eff_amp_res;
            cur->coupled = coupled;
        }
    }
    return bits;
}

static int write_sbr_data(SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs, int id_aac, int ch0, int sendHeader, bool write)
{
    int nch = (id_aac == ID_CPE) ? 2 : 1;
    int trial = !write && nch == 2;
    int cost[2], bits;

    if (trial) {
        couple_envelopes(sbr, fd, ch0);
        sbr->coupled = 1;
    } else if (!write) {
        sbr->coupled = 0;
    }
    for (int pass = 0;; pass++) {
        bits = write_sbr_channels(sbr, fd, bs, nch, ch0, sendHeader, sbr->coupled, write);
        if (!trial || pass == 2) return bits;
        cost[sbr->coupled] = bits;
        if (pass == 1 && cost[1] >= cost[0]) return bits;
        sbr->coupled = pass;
    }
}

static int emit_sbr_payload(SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs, int id_aac, int ch0, int sendHeader, bool write)
{
    uint32_t startBits = (write && bs) ? bs->currentBit : 0;
    int bits = 5;
    if (write) PutBit(bs, (SBR_EXT_TYPE_SBR << 1) | (sendHeader & 1), 5);
    if (sendHeader) bits += write_sbr_header(sbr, bs, write);
    bits += write_sbr_data(sbr, fd, bs, id_aac, ch0, sendHeader, write);
    if (write && bs) {
        uint32_t writtenBits = bs->currentBit - startBits;
        assert(writtenBits == (uint32_t)bits);
    }
    return bits;
}

static int SbrWrite(SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs, int id_aac, int ch0)
{
    if (!sbr || !sbr->sbrPresent) return 0;

    int sendHeader = sbr->sendHeaderThisFrame;

    int payloadBits = emit_sbr_payload(sbr, fd, NULL, id_aac, ch0, sendHeader, false);
    int fillBytes = (payloadBits + 7) / 8;
    int padBits = fillBytes * 8 - payloadBits;

    assert(fillBytes <= 14 + 255);

    int totalBits;
    PutBit(bs, ID_FIL, 3);
    if (fillBytes < 15) {
        PutBit(bs, fillBytes, 4);
        totalBits = 7;
    } else {
        PutBit(bs, 15, 4);
        PutBit(bs, fillBytes - 14, 8);
        totalBits = 15;
    }
    emit_sbr_payload(sbr, fd, bs, id_aac, ch0, sendHeader, true);
    if (padBits > 0) PutBit(bs, 0, padBits);

    return totalBits + payloadBits + padBits;
}

int SbrContextGetBits(SBRContext *sCtx, BitStream *bs, const AACElement *elem, int aacObjectType)
{
    if (aacObjectType == HE_V1 && sCtx && elem->type != ID_LFE) {
        if (sCtx->sbrInfo) {
            int id_aac = (elem->type == ID_CPE) ? ID_CPE : ID_SCE;
            const SbrFrameData *fd = &sCtx->frameFIFO[(sCtx->frameHead + 1) % SBR_FRAME_FIFO];
            SBRInfo *sbr = sCtx->sbrInfo;
            if (!sbr->headerDecided) {
                sbr->sendHeaderThisFrame = (sbr->frameCount++ % SBR_HEADER_PERIOD == 0);
                sbr->headerDecided = 1;
            }
            return SbrWrite(sbr, fd, bs, id_aac, elem->channels[0]);
        }
    }
    return 0;
}
