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
#include <limits.h>
#include <string.h>

#include "sbr.h"
#include "sbr_internal.h"
#include "sbr_tables.h"
#include "bitstream.h"
#include "channels.h"
#include "util.h"
#include "faac_internal.h"

typedef struct {
    unsigned envDt[2];
    unsigned noiseDt[2];
    int coupled, noiseLinked[2], linked[2], bits;
} SbrDecision;

static void emit_sbr_header(const SBRInfo *sbr, BitStream *bs)
{
    /* ISO 14496-3:2009 §4.6.18.5 sbr_header() (21 bits) */
    PutBit(bs, SBR_AMP_RES, 1); PutBit(bs, sbr->bs_start_freq, 4);
    PutBit(bs, sbr->bs_stop_freq, 4); PutBit(bs, sbr->bs_xover_band, 3);
    PutBit(bs, 0, 2); PutBit(bs, 1, 1); PutBit(bs, 0, 1);
    PutBit(bs, sbr->bs_freq_scale, 2); PutBit(bs, sbr->bs_alter_scale, 1);
    PutBit(bs, 0, 2);
}

/* Width of the transient pointer field, indexed by number of envelopes. */
static const int sbr_ceil_log2[] = { 0, 1, 2, 2, 3, 3 };

static int sbr_grid_varvar_split(const SbrGrid *grid)
{
    int num_env = grid->numEnvelopes;
    int first = num_env - 4;

    if (first < 0) first = 0;
    for (int n0 = first; n0 <= 3 && n0 < num_env; n0++) {
        int n1 = num_env - 1 - n0;
        int valid = n1 <= 3;
        for (int i = 0; valid && i < n0; i++) {
            int d = grid->tEnv[i + 1] - grid->tEnv[i];
            valid = d >= 2 && d <= 8 && !(d & 1);
        }
        for (int i = 0; valid && i < n1; i++) {
            int d = grid->tEnv[num_env - i] - grid->tEnv[num_env - i - 1];
            valid = d >= 2 && d <= 8 && !(d & 1);
        }
        if (valid) return n0;
    }
    return -1;
}

static int sbr_grid_bits(const SbrGrid *grid)
{
    int num_env = grid->numEnvelopes;
    int bits = 2;

    assert(num_env >= 1 && num_env <= SBR_MAX_ENVELOPES);
    if (grid->frameClass == SBR_FRAME_CLASS_FIXFIX) {
        assert(num_env == 1 || num_env == 2 || num_env == 4);
        for (int e = 1; e < num_env; e++) assert(grid->freqRes[e] == grid->freqRes[0]);
        bits += 3;
    } else if (grid->frameClass == SBR_FRAME_CLASS_FIXVAR ||
               grid->frameClass == SBR_FRAME_CLASS_VARFIX) {
        assert(num_env <= 4);
        assert(grid->bsPointer >= 0 && grid->bsPointer < (1 << sbr_ceil_log2[num_env]));
        bits += 4 + 2 * (num_env - 1) + sbr_ceil_log2[num_env] + num_env;
    } else {
        assert(grid->frameClass == SBR_FRAME_CLASS_VARVAR);
        assert(sbr_grid_varvar_split(grid) >= 0);
        assert(grid->bsPointer >= 0 && grid->bsPointer < (1 << sbr_ceil_log2[num_env]));
        bits += 8 + 2 * (num_env - 1) + sbr_ceil_log2[num_env] + num_env;
    }
    return bits;
}

static void emit_sbr_grid(const SbrGrid *grid, BitStream *bs)
{
    int num_env = grid->numEnvelopes;
    PutBit(bs, grid->frameClass, 2);

    if (grid->frameClass == SBR_FRAME_CLASS_FIXFIX) {
        int bs_num_env = num_env == 1 ? 0 : num_env == 2 ? 1 : 2;
        PutBit(bs, bs_num_env, 2);
        PutBit(bs, grid->freqRes[0], 1);
    } else if (grid->frameClass == SBR_FRAME_CLASS_FIXVAR) {
        PutBit(bs, grid->tEnv[num_env] - SBR_NUM_TIME_SLOTS, 2);
        PutBit(bs, num_env - 1, 2);
        for (int i = 0; i < num_env - 1; i++)
            PutBit(bs, (grid->tEnv[num_env - i] - grid->tEnv[num_env - i - 1] - 2) / 2, 2);
        PutBit(bs, grid->bsPointer, sbr_ceil_log2[num_env]);
        for (int i = num_env - 1; i >= 0; i--) PutBit(bs, grid->freqRes[i], 1);
    } else if (grid->frameClass == SBR_FRAME_CLASS_VARFIX) {
        PutBit(bs, grid->tEnv[0], 2); PutBit(bs, num_env - 1, 2);
        for (int i = 0; i < num_env - 1; i++) PutBit(bs, (grid->tEnv[i + 1] - grid->tEnv[i] - 2) / 2, 2);
        PutBit(bs, grid->bsPointer, sbr_ceil_log2[num_env]);
        for (int i = 0; i < num_env; i++) PutBit(bs, grid->freqRes[i], 1);
    } else {
        int n0 = sbr_grid_varvar_split(grid);
        int n1 = num_env - 1 - n0;
        PutBit(bs, grid->tEnv[0], 2);
        PutBit(bs, grid->tEnv[num_env] - SBR_NUM_TIME_SLOTS, 2);
        PutBit(bs, n0, 2); PutBit(bs, n1, 2);
        for (int i = 0; i < n0; i++)
            PutBit(bs, (grid->tEnv[i + 1] - grid->tEnv[i] - 2) / 2, 2);
        for (int i = 0; i < n1; i++)
            PutBit(bs, (grid->tEnv[num_env - i] - grid->tEnv[num_env - i - 1] - 2) / 2, 2);
        PutBit(bs, grid->bsPointer, sbr_ceil_log2[num_env]);
        for (int i = 0; i < num_env; i++) PutBit(bs, grid->freqRes[i], 1);
    }
}

/* bs_df_env from the choices the sizing pass cached; bs_df_noise whenever a
 * reference exists (see write_sbr_noise). */
static void emit_sbr_dtdf(const SbrGrid *grid, const SbrDecision *d, int ch, BitStream *bs)
{
    int n_q = grid->numEnvelopes > 1 ? 2 : 1;
    for (int e = 0; e < grid->numEnvelopes; e++) PutBit(bs, (d->envDt[ch] >> e) & 1, 1);
    for (int q = 0; q < n_q; q++) PutBit(bs, (d->noiseDt[ch] >> q) & 1, 1);
}

static void emit_sbr_invf(BitStream *bs)
{
    PutBit(bs, SBR_INVF_MODE, 2);
}

typedef struct { const SBRHuffEntry *f, *t; int lav, start; } SbrDeltaBook;

static const SbrDeltaBook sbr_env_books[2][2] = {
    { { f_huff_env_1_5dB, t_huff_env_1_5dB, F_HUFF_ENV_1_5DB_OFFSET, 7 }, { f_huff_env_3_0dB, t_huff_env_3_0dB, F_HUFF_ENV_3_0DB_OFFSET, 6 } },
    { { f_huff_env_bal_1_5dB, t_huff_env_bal_1_5dB, F_HUFF_ENV_BAL_1_5DB_OFFSET, 6 }, { f_huff_env_bal_3_0dB, t_huff_env_bal_3_0dB, F_HUFF_ENV_BAL_3_0DB_OFFSET, 5 } },
};

/* NULL tables describe the fixed-width noise floor codes. */
static int code_deltas(const int *values, const int *previous, int count, int time,
                       const SbrDeltaBook *book, BitStream *bs)
{
    const SBRHuffEntry *tab = time ? book->t : book->f;
    int lav = time ? T_HUFF_ENV_LAV : book->lav;
    int bits = time ? 0 : book->start, i = time ? 0 : 1;
    if (!time && bs) PutBit(bs, values[0], book->start);
    for (; i < count; i++) {
        int d = values[i] - (time ? previous[i] : values[i - 1]);
        if (!tab) { if (bs) PutBit(bs, 0, 1); return bits + 1; }
        if (d < -lav || d > lav) return INT_MAX;
        bits += tab[d + lav] & 31;
        if (bs) PutBit(bs, tab[d + lav] >> 5, tab[d + lav] & 31);
    }
    return bits;
}

/* Each envelope takes the cheaper of frequency and time deltas; a frame's
 * first envelope refers to the channel's last written one, when linked. The
 * sizing pass (write false) makes the choice and caches it for the write pass,
 * which always follows it with the same frame. */
static int choose_sbr_envelope(const SBRInfo *sbr, const SbrFrameData *fd, const SbrGrid *grid, const int (*env)[SBR_MAX_BANDS],
                               int bal, int linked, int ch, unsigned *chosen)
{
    int bits = 0;

    for (int e = 0; e < grid->numEnvelopes; e++) {
        const int *ref = e ? env[e - 1] : sbr->ch[ch].ref[~sbr->frameCount & 1].env;
        int best = INT_MAX;
        int timeReference = e ? grid->freqRes[e] == grid->freqRes[e - 1] : linked;
        for (int t = 0; t <= timeReference; t++) {
            int n = code_deltas(env[e], ref, sbr_env_bands(sbr, grid, e), t, &sbr_env_books[bal][fd->ch[ch].eff_amp_res], NULL);
            if (n < best) { best = n; *chosen = (*chosen & ~(1u << e)) | ((unsigned)t << e); }
        }
        if (best == INT_MAX) { bits = INT_MAX; break; }
        bits += best;
    }
    return bits;
}

static void emit_sbr_envelope(const SBRInfo *sbr, const SbrFrameData *fd, const SbrGrid *grid, const int (*env)[SBR_MAX_BANDS],
                              int bal, int ch, unsigned dt, BitStream *bs)
{
    for (int e = 0; e < grid->numEnvelopes; e++) {
        const int *ref = e ? env[e - 1] : sbr->ch[ch].ref[~sbr->frameCount & 1].env;
        code_deltas(env[e], ref, sbr_env_bands(sbr, grid, e), dt >> e & 1, &sbr_env_books[bal][fd->ch[ch].eff_amp_res], bs);
    }
}

/* One noise band at a constant level: 5-bit absolute (the level, or for a
 * coupled balance channel the centre 6), or the time delta 0, whose code is
 * the single bit 0 -- always the cheaper once a reference exists. */
static void emit_sbr_noise(const SbrGrid *grid, int value, unsigned dt, BitStream *bs)
{
    static const SbrDeltaBook noise = { NULL, NULL, 0, 5 };
    int n_q = grid->numEnvelopes > 1 ? 2 : 1;
    for (int q = 0; q < n_q; q++) code_deltas(&value, &value, 1, dt >> q & 1, &noise, bs);
}

/* Coupled rendition of a pair from the channels' own levels: the level of
 * (L + R) / 2, and the balance L - R at half resolution around its centre. */
static void couple_envelopes(SBRInfo *sbr, const SbrFrameData *fd, int ch0)
{
    int amp = fd->ch[ch0].eff_amp_res;
    int pan = amp ? 12 : 24;
    const SbrGrid *grid = &fd->ch[ch0].grid;

    for (int e = 0; e < grid->numEnvelopes; e++) {
        int nb = sbr_env_bands(sbr, grid, e);
        const int *restrict left = fd->ch[ch0].envData[e], *restrict right = fd->ch[ch0 + 1].envData[e];
        int *restrict level = sbr->cplEnv[0][e], *restrict balance = sbr->cplEnv[1][e];
        for (int b = 0; b < nb; b++) {
            int l = left[b], r = right[b];
            int d = l > r ? l - r : r - l;
            /* the louder side plus log2((1 + 2^-d) / 2) in level steps, rounded */
            level[b] = (l > r ? l : r) - (d >= 2) - (!amp && d >= 5);
            balance[b] = (clamp_int(pan + l - r, 0, 2 * pan) + 1) >> 1;
        }
    }
}

static int choose_sbr_channels(SBRInfo *sbr, const SbrFrameData *fd, int nch, int ch0,
                               int sendHeader, SbrDecision *d)
{
    int bits = (nch == 2) ? 5 : 3; /* data_extra, coupling, add_harmonic flags, ext data flag */
    const int (*env[2])[SBR_MAX_BANDS];
    int ngrid = d->coupled ? 1 : nch;

    for (int ch = 0; ch < nch; ch++)
        env[ch] = d->coupled ? (const int (*)[SBR_MAX_BANDS])sbr->cplEnv[ch]
                          : (const int (*)[SBR_MAX_BANDS])fd->ch[ch0 + ch].envData;

    for (int ch = 0; ch < ngrid; ch++) bits += sbr_grid_bits(&fd->ch[ch0 + ch].grid);
    for (int ch = 0; ch < nch; ch++) {
        const SbrGrid *grid = &fd->ch[ch0 + ch].grid;
        const SbrEnvRef *prev = &sbr->ch[ch0 + ch].ref[~sbr->frameCount & 1];
        d->noiseLinked[ch] = !sendHeader && prev->nb && prev->coupled == d->coupled;
        /* Link only when the previous last envelope matches this first one. */
        d->linked[ch] = d->noiseLinked[ch] && prev->lastFreqRes == grid->freqRes[0] && prev->ampRes == fd->ch[ch0 + ch].eff_amp_res;
        bits += grid->numEnvelopes + (grid->numEnvelopes > 1 ? 2 : 1);
    }
    for (int ch = 0; ch < ngrid; ch++)
        bits += 2;
    /* Coupled: envelope and noise per channel in turn; otherwise all
     * envelopes, then all noise floors. */
    for (int k = 0; k < 2 * nch; k++) {
        int ch = d->coupled ? k >> 1 : k % nch;
        if (d->coupled ? k & 1 : k >= nch) {
            const SbrGrid *grid = &fd->ch[ch0 + ch].grid;
            int n_q = grid->numEnvelopes > 1 ? 2 : 1;
            d->noiseDt[ch] = 0;
            for (int q = 0; q < n_q; q++) d->noiseDt[ch] |= (unsigned)(q || d->noiseLinked[ch]) << q;
            for (int q = 0; q < n_q; q++) bits += (d->noiseDt[ch] >> q & 1) ? 1 : 5;
        } else {
            d->envDt[ch] = 0;
            int n = choose_sbr_envelope(sbr, fd, &fd->ch[ch0 + ch].grid, env[ch], d->coupled && ch, d->linked[ch], ch0 + ch, &d->envDt[ch]);
            if (n == INT_MAX) return INT_MAX;
            bits += n;
        }
    }
    return bits;
}

static int emit_sbr_channels(SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs, int nch, int ch0, const SbrDecision *d)
{
    int ngrid = d->coupled ? 1 : nch;
    const int (*env[2])[SBR_MAX_BANDS];
    PutBit(bs, d->coupled, nch);
    for (int ch = 0; ch < nch; ch++) env[ch] = d->coupled ? (const int (*)[SBR_MAX_BANDS])sbr->cplEnv[ch] : (const int (*)[SBR_MAX_BANDS])fd->ch[ch0 + ch].envData;
    for (int ch = 0; ch < ngrid; ch++) emit_sbr_grid(&fd->ch[ch0 + ch].grid, bs);
    for (int ch = 0; ch < nch; ch++) emit_sbr_dtdf(&fd->ch[ch0 + ch].grid, d, ch, bs);
    for (int ch = 0; ch < ngrid; ch++) emit_sbr_invf(bs);
    for (int k = 0; k < 2 * nch; k++) {
        int ch = d->coupled ? k >> 1 : k % nch;
        const SbrGrid *grid = &fd->ch[ch0 + ch].grid;
        if (d->coupled ? k & 1 : k >= nch) emit_sbr_noise(grid, d->coupled && ch ? 6 : SBR_NOISE_LEVEL_DEFAULT, d->noiseDt[ch], bs);
        else emit_sbr_envelope(sbr, fd, grid, env[ch], d->coupled && ch, ch0 + ch, d->envDt[ch], bs);
    }
    PutBit(bs, 0, nch + 1);
    for (int ch = 0; ch < nch; ch++) {
        const SbrGrid *grid = &fd->ch[ch0 + ch].grid;
        int last = grid->numEnvelopes - 1;
        int nb = sbr_env_bands(sbr, grid, last);
        SbrEnvRef *cur = &sbr->ch[ch0 + ch].ref[sbr->frameCount & 1];
        memcpy(cur->env, env[ch][last], nb * sizeof(int));
        cur->nb = nb; cur->ampRes = fd->ch[ch0 + ch].eff_amp_res; cur->coupled = d->coupled;
        cur->trailingBorder = grid->tEnv[grid->numEnvelopes];
        cur->lastFreqRes = grid->freqRes[last];
    }
    return d->bits;
}

/* A pair is coupled when that codes smaller. The sizing pass costs both
 * layouts and ends on the chosen one, so the channel caches match it.
 * One call site in a loop, so -O3 has no constant nch to clone on. */
static int choose_sbr_data(SBRInfo *sbr, const SbrFrameData *fd, int id_aac, int ch0, int sendHeader, SbrDecision *d)
{
    int nch = (id_aac == ID_CPE) ? 2 : 1;
    SbrDecision cand[2] = { { .coupled = 0 } };
    int n = 1;

    if (nch == 2) {
        /* A coupled channel reuses channel 0's grid. */
        const SbrGrid *left = &fd->ch[ch0].grid, *right = &fd->ch[ch0 + 1].grid;
        int sameGrid = left->frameClass == right->frameClass && left->numEnvelopes == right->numEnvelopes && left->bsPointer == right->bsPointer;
        for (int e = 0; sameGrid && e <= left->numEnvelopes; e++) sameGrid = left->tEnv[e] == right->tEnv[e];
        for (int e = 0; sameGrid && e < left->numEnvelopes; e++) sameGrid = left->freqRes[e] == right->freqRes[e];
        if (sameGrid) {
            cand[1] = (SbrDecision){ .coupled = 1 };
            couple_envelopes(sbr, fd, ch0);
            n = 2;
        }
    }
    int best = 0;
    for (int i = 0; i < n; i++) {
        cand[i].bits = choose_sbr_channels(sbr, fd, nch, ch0, sendHeader, &cand[i]);
        if (i && cand[i].bits <= cand[best].bits) best = i; /* ties favor coupled */
    }
    *d = cand[best];
    return d->bits;
}

/* Emit the full extension_payload body for EXT_SBR_DATA: the 4-bit extension
 * type, the 1-bit header flag, the optional header, and the channel data. */
static int choose_sbr_payload(SBRInfo *sbr, const SbrFrameData *fd, int id_aac, int ch0, int sendHeader, SbrDecision *d)
{
    int bits = 5;
    if (sendHeader) bits += 21;
    bits += choose_sbr_data(sbr, fd, id_aac, ch0, sendHeader, d);
    return bits;
}

static int emit_sbr_payload(SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs, int id_aac, int ch0, int sendHeader, const SbrDecision *d)
{
    PutBit(bs, (SBR_EXT_TYPE_SBR << 1) | (sendHeader & 1), 5);
    if (sendHeader) emit_sbr_header(sbr, bs);
    return 5 + (sendHeader ? 21 : 0) + emit_sbr_channels(sbr, fd, bs, id_aac == ID_CPE ? 2 : 1, ch0, d);
}

static int SbrWrite(SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs, int id_aac, int ch0)
{
    if (!sbr || !sbr->sbrPresent) return 0;

    int sendHeader = sbr->sendHeaderThisFrame;

    /* The fill_element's cnt field must precede the payload in the bitstream,
     * so its size is needed before anything is written. Re-deriving it with a
     * dry (write=false) pass is cheap -- a few hundred fixed-width/Huffman
     * fields, not a hot loop -- re-deriving it from sbr's already-quantized
     * envelope/noise data. */
    SbrDecision decision = {0};
    int payloadBits = choose_sbr_payload(sbr, fd, id_aac, ch0, sendHeader, &decision);
    int fillBytes = (payloadBits + 7) / 8;
    int padBits = fillBytes * 8 - payloadBits;

    /* The fill_element count escapes through an 8-bit field, so a single
     * extension_payload tops out at 15 + 255 - 1 = 269 bytes. A larger SBR
     * payload would silently truncate esc_count and corrupt the boundary. */
    assert(fillBytes <= 14 + 255);

    int totalBits;
    /* fill_element(): id, then 4-bit count with optional 8-bit escape.
     * The decoder reconstructs cnt = 15 + esc_count - 1, hence
     * esc_count = N - 14. */
    PutBit(bs, ID_FIL, 3);
    if (fillBytes < 15) {
        PutBit(bs, fillBytes, 4);
        totalBits = 7;
    } else {
        PutBit(bs, 15, 4);
        PutBit(bs, fillBytes - 14, 8);
        totalBits = 15;
    }
    uint32_t payloadStart = bs->currentBit;
    int emittedBits = emit_sbr_payload(sbr, fd, bs, id_aac, ch0, sendHeader, &decision);
    assert(payloadBits == emittedBits);
    assert(bs->currentBit - payloadStart == (uint32_t)payloadBits);
    if (padBits > 0) PutBit(bs, 0, padBits);

    return totalBits + payloadBits + padBits;
}

int SbrContextGetBits(SBRContext *sCtx, BitStream *bs, const AACElement *elem, int aacObjectType)
{
    if (aacObjectType == HE_V1 && sCtx && elem->type != ID_LFE) {
        if (sCtx->sbrInfo) {
            int id_aac = (elem->type == ID_CPE) ? ID_CPE : ID_SCE;
            /* One step past the newest slot is the oldest: the payload whose
             * audio this access unit's core carries. See SBR_FRAME_FIFO. */
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
