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

/* ISO 14496-3:2009 §4.6.18.5 sbr_header() (21 bits) */
static void write_sbr_header(const SBRInfo *sbr, BitStream *bs)
{
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

/* Width of the transient pointer field, indexed by number of envelopes. */
static const int sbr_ceil_log2[] = { 0, 1, 2, 2, 3, 3 };

static int size_sbr_grid(const SbrFrameData *fd)
{
    int num_env = fd->numEnvelopes;
    if (fd->frameClass == SBR_FRAME_CLASS_VARFIX)
        return 4 + 2 * (num_env - 1) + sbr_ceil_log2[num_env] + num_env;
    return 3;
}

static void write_sbr_grid(const SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs)
{
    int num_env = fd->numEnvelopes;
    PutBit(bs, fd->frameClass, 2);
    if (fd->frameClass == SBR_FRAME_CLASS_VARFIX) {
        PutBit(bs, fd->tEnv[0], 2);
        PutBit(bs, num_env - 1, 2);
        for (int i = 0; i < num_env - 1; i++)
            PutBit(bs, (fd->tEnv[i + 1] - fd->tEnv[i] - 2) / 2, 2);
        PutBit(bs, fd->bsPointer, sbr_ceil_log2[num_env]);
        for (int i = 0; i < num_env; i++)
            PutBit(bs, sbr->bs_freq_res, 1);
    } else {
        PutBit(bs, num_env > 1 ? 1 : 0, 2);
        PutBit(bs, sbr->bs_freq_res, 1);
    }
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

struct EnvBooks {
    const SBRHuffEntry *f, *t;
    int lav, nsyms, start;
};

static const struct EnvBooks books[2][2] = {
    { { f_huff_env_1_5dB, t_huff_env_1_5dB, F_HUFF_ENV_1_5DB_OFFSET, F_HUFF_ENV_1_5DB_NSYMS, 7 },
      { f_huff_env_3_0dB, t_huff_env_3_0dB, F_HUFF_ENV_3_0DB_OFFSET, F_HUFF_ENV_3_0DB_NSYMS, 6 } },
    { { f_huff_env_bal_1_5dB, t_huff_env_bal_1_5dB, F_HUFF_ENV_BAL_1_5DB_OFFSET, F_HUFF_ENV_BAL_1_5DB_NSYMS, 6 },
      { f_huff_env_bal_3_0dB, t_huff_env_bal_3_0dB, F_HUFF_ENV_BAL_3_0DB_OFFSET, F_HUFF_ENV_BAL_3_0DB_NSYMS, 5 } },
};

/* Generic delta cost estimation: returns bits for coding cur[0..nb-1].
 * If ref is non-NULL, calculates time deltas (returning INT_MAX if delta exceeds LAV).
 * If ref is NULL, calculates frequency deltas, clamping cur[0] and deltas as master did. */
static int cost_delta_run(const int *cur, const int *ref, int nb,
                          const SBRHuffEntry *table, int offset, int nsyms, int first_bits)
{
    int bits = 0;
    if (ref) {
        for (int b = 0; b < nb; b++) {
            int d = cur[b] - ref[b];
            if (d < -offset || d > offset) return INT_MAX;
            bits += sbr_huff_len(table[d + offset]);
        }
    } else {
        bits = first_bits;
        for (int b = 1; b < nb; b++) {
            int d = cur[b] - cur[b - 1];
            int idx = clamp_int(d + offset, 0, nsyms - 1);
            bits += sbr_huff_len(table[idx]);
        }
    }
    return bits;
}

/* Generic delta write emitter: emits symbols into BitAccumulator acc. */
static void write_delta_run(BitAccumulator *acc, const int *cur, const int *ref, int nb,
                            const SBRHuffEntry *table, int offset, int nsyms, int first_bits)
{
    if (ref) {
        for (int b = 0; b < nb; b++) {
            int d = cur[b] - ref[b];
            int idx = d + offset;
            AccumPutBits(acc, sbr_huff_code(table[idx]), sbr_huff_len(table[idx]));
        }
    } else {
        int first_max = (1 << first_bits) - 1;
        AccumPutBits(acc, (uint32_t)clamp_int(cur[0], 0, first_max), first_bits);
        for (int b = 1; b < nb; b++) {
            int d = cur[b] - cur[b - 1];
            int idx = clamp_int(d + offset, 0, nsyms - 1);
            AccumPutBits(acc, sbr_huff_code(table[idx]), sbr_huff_len(table[idx]));
        }
    }
}

/* Sizing pass: calculates envelope costs and records the dt decision mask into *out_dt */
static int size_sbr_envelope(const SBRInfo *sbr, const SbrFrameData *fd,
                             const int envData[SBR_MAX_ENVELOPES][SBR_MAX_BANDS],
                             int linked, int ch, int bal, unsigned *out_dt)
{
    const struct EnvBooks *bk = &books[bal][fd->eff_amp_res];
    const SBRChannel *sc = &sbr->ch[ch];
    int nb = sbr_env_bands(sbr, fd);
    unsigned chosen = 0;
    int bits = 0;

    for (int e = 0; e < fd->numEnvelopes; e++) {
        const int *env = envData[e];
        int freq_cost = cost_delta_run(env, NULL, nb, bk->f, bk->lav, bk->nsyms, bk->start);
        int time_cost = INT_MAX;

        if (e > 0 || linked) {
            const int *ref = e ? envData[e - 1] : sc->ref[~sbr->frameCount & 1].env;
            time_cost = cost_delta_run(env, ref, nb, bk->t, T_HUFF_ENV_LAV, T_HUFF_ENV_NSYMS, 0);
        }

        if (time_cost < freq_cost) {
            bits += time_cost;
            chosen |= (1u << e);
        } else {
            if (freq_cost == INT_MAX) return INT_MAX;
            bits += freq_cost;
        }
    }
    if (out_dt) *out_dt = chosen;
    return bits;
}

/* Write pass: emits envelope symbols per recorded dt decision mask sc->envDt */
static void write_sbr_envelope(const SBRInfo *sbr, const SbrFrameData *fd,
                               const int envData[SBR_MAX_ENVELOPES][SBR_MAX_BANDS],
                               BitAccumulator *acc, int linked, int ch, int bal)
{
    const struct EnvBooks *bk = &books[bal][fd->eff_amp_res];
    const SBRChannel *sc = &sbr->ch[ch];
    int nb = sbr_env_bands(sbr, fd);

    for (int e = 0; e < fd->numEnvelopes; e++) {
        const int *env = envData[e];
        int use_dt = (sc->envDt >> e) & 1;
        if (use_dt) {
            const int *ref = e ? envData[e - 1] : sc->ref[~sbr->frameCount & 1].env;
            write_delta_run(acc, env, ref, nb, bk->t, T_HUFF_ENV_LAV, T_HUFF_ENV_NSYMS, 0);
        } else {
            write_delta_run(acc, env, NULL, nb, bk->f, bk->lav, bk->nsyms, bk->start);
        }
    }
}

static void write_sbr_noise(BitStream *bs, const SbrFrameData *fd, int noise_val, int noiseLinked)
{
    int n_q = fd->numEnvelopes > 1 ? 2 : 1;
    for (int q = 0; q < n_q; q++) {
        int len = (q || noiseLinked) ? 1 : 5;
        PutBit(bs, len == 1 ? 0 : noise_val, len);
    }
}

/* Sizing pass: computes payload cost and records decisions (coupled, envDt) */
static int size_sbr_data(SBRInfo *sbr, const SbrFrameData *fd, int id_aac, int ch0, int sendHeader)
{
    int nch = (id_aac == ID_CPE) ? 2 : 1;
    int nb = sbr_env_bands(sbr, fd);
    int grid_bits = size_sbr_grid(fd);
    int n_q = fd->numEnvelopes > 1 ? 2 : 1;

    const SBRChannel *sc0 = &sbr->ch[ch0];
    const SbrEnvRef *prev = &sc0->ref[~sbr->frameCount & 1];

    /* Uncoupled layout */
    int noiseLinked_0 = !sendHeader && prev->nb && !prev->coupled;
    int linked_0 = noiseLinked_0 && prev->nb == nb && prev->ampRes == fd->eff_amp_res;
    int noise_bits_0 = n_q * (noiseLinked_0 ? 1 : 5);

    int cost_uncoupled = (nch == 2 ? 5 : 3) + nch * (grid_bits + fd->numEnvelopes + n_q + 2 + noise_bits_0);
    unsigned dt_uncoupled[2] = {0, 0};

    for (int ch = 0; ch < nch; ch++) {
        int n = size_sbr_envelope(sbr, fd, fd->ch[ch0 + ch].envData, linked_0, ch0 + ch, 0, &dt_uncoupled[ch]);
        if (n == INT_MAX) cost_uncoupled = INT_MAX;
        else cost_uncoupled += n;
    }

    if (nch == 1) {
        sbr->coupled = 0;
        sbr->ch[ch0].envDt = dt_uncoupled[0];
        return cost_uncoupled;
    }

    /* Coupled layout for CPE */
    couple_envelopes(sbr, fd, ch0);
    int noiseLinked_1 = !sendHeader && prev->nb && prev->coupled;
    int linked_1 = noiseLinked_1 && prev->nb == nb && prev->ampRes == fd->eff_amp_res;
    int noise_bits_1 = n_q * (noiseLinked_1 ? 1 : 5);

    int cost_coupled = 5 + (1 * grid_bits) + (2 * (fd->numEnvelopes + n_q)) + (1 * 2) + (2 * noise_bits_1);
    unsigned dt_coupled[2] = {0, 0};

    int n0 = size_sbr_envelope(sbr, fd, (const int (*)[SBR_MAX_BANDS])sbr->cplEnv[0], linked_1, ch0, 0, &dt_coupled[0]);
    int n1 = size_sbr_envelope(sbr, fd, (const int (*)[SBR_MAX_BANDS])sbr->cplEnv[1], linked_1, ch0 + 1, 1, &dt_coupled[1]);

    if (n0 == INT_MAX || n1 == INT_MAX) cost_coupled = INT_MAX;
    else cost_coupled += n0 + n1;

    if (cost_coupled < cost_uncoupled) {
        sbr->coupled = 1;
        sbr->ch[ch0].envDt = dt_coupled[0];
        sbr->ch[ch0 + 1].envDt = dt_coupled[1];
        return cost_coupled;
    } else {
        sbr->coupled = 0;
        sbr->ch[ch0].envDt = dt_uncoupled[0];
        sbr->ch[ch0 + 1].envDt = dt_uncoupled[1];
        return cost_uncoupled;
    }
}

/* Write pass: emits bitstream symbols using recorded decisions (coupled, envDt) */
static void write_sbr_data(SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs, int id_aac, int ch0, int sendHeader)
{
    int nch = (id_aac == ID_CPE) ? 2 : 1;
    int coupled = (nch == 2) && sbr->coupled;
    int nb = sbr_env_bands(sbr, fd);

    const SBRChannel *sc0 = &sbr->ch[ch0];
    const SbrEnvRef *prev = &sc0->ref[~sbr->frameCount & 1];
    int noiseLinked = !sendHeader && prev->nb && (prev->coupled == coupled);
    int linked = noiseLinked && prev->nb == nb && prev->ampRes == fd->eff_amp_res;

    const int (*env[2])[SBR_MAX_BANDS];
    for (int ch = 0; ch < nch; ch++)
        env[ch] = coupled ? (const int (*)[SBR_MAX_BANDS])sbr->cplEnv[ch]
                          : (const int (*)[SBR_MAX_BANDS])fd->ch[ch0 + ch].envData;

    PutBit(bs, coupled, nch == 2 ? 2 : 1);

    int ngrid = coupled ? 1 : nch;
    for (int ch = 0; ch < ngrid; ch++)
        write_sbr_grid(sbr, fd, bs);

    int n_q = fd->numEnvelopes > 1 ? 2 : 1;
    for (int ch = 0; ch < nch; ch++) {
        SBRChannel *sc = &sbr->ch[ch0 + ch];
        for (int e = 0; e < fd->numEnvelopes; e++)
            PutBit(bs, (sc->envDt >> e) & 1, 1);
        for (int q = 0; q < n_q; q++)
            PutBit(bs, q || noiseLinked, 1);
    }

    for (int ch = 0; ch < ngrid; ch++)
        PutBit(bs, SBR_INVF_MODE, 2);

    BitAccumulator acc = {0};
    AccumBegin(&acc, bs);

    for (int k = 0; k < 2 * nch; k++) {
        int ch = coupled ? (k >> 1) : (k % nch);
        int is_noise = coupled ? (k & 1) : (k >= nch);

        if (is_noise) {
            AccumEnd(&acc);
            int noise_val = (coupled && ch) ? 6 : SBR_NOISE_LEVEL_DEFAULT;
            write_sbr_noise(bs, fd, noise_val, noiseLinked);
            AccumBegin(&acc, bs);
        } else {
            write_sbr_envelope(sbr, fd, env[ch], &acc, linked, ch0 + ch, coupled && ch);
        }
    }
    AccumEnd(&acc);

    PutBit(bs, 0, nch == 2 ? 3 : 2);

    for (int ch = 0; ch < nch; ch++) {
        SbrEnvRef *cur = &sbr->ch[ch0 + ch].ref[sbr->frameCount & 1];
        memcpy(cur->env, env[ch][fd->numEnvelopes - 1], nb * sizeof(int));
        cur->nb = nb;
        cur->ampRes = fd->eff_amp_res;
        cur->coupled = coupled;
    }
}

static int emit_sbr_payload(SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs, int id_aac, int ch0, int sendHeader, bool write)
{
    if (write) {
        PutBit(bs, (SBR_EXT_TYPE_SBR << 1) | (sendHeader & 1), 5);
        if (sendHeader) write_sbr_header(sbr, bs);
        write_sbr_data(sbr, fd, bs, id_aac, ch0, sendHeader);
        return 0;
    }
    int bits = 5;
    if (sendHeader) bits += 21;
    bits += size_sbr_data(sbr, fd, id_aac, ch0, sendHeader);
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
