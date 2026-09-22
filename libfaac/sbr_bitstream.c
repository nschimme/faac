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

static int write_sbr_header(const SBRInfo *sbr, BitStream *bs, bool write)
{
    if (write) {
        /* ISO 14496-3:2009 §4.6.18.5 sbr_header() (21 bits) */
        PutBit(bs, sbr->bs_amp_res,     1); /* bs_amp_res: 0=1.5dB, 1=3dB */
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
        /* VARFIX (§4.6.18.3.6): variable leading borders, fixed (untransmitted)
         * trailing border at numTimeSlots, then bs_pointer and per-envelope
         * bs_freq_res. */
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
        /* FIXFIX: equal-spaced borders (not transmitted, the decoder derives
         * them from the envelope count), one bs_freq_res for all envelopes. */
        if (write) {
            PutBit(bs, num_env > 1 ? 1 : 0, 2);
            PutBit(bs, sbr->bs_freq_res, 1);
        }
        bits += 3;
    }
    return bits;
}

/* bs_df_env from the choices the sizing pass cached; bs_df_noise whenever a
 * reference exists (see write_sbr_noise). */
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

/* Codes one envelope as time deltas against ref, or as an absolute first band
 * plus frequency deltas; returns its bits, INT_MAX when a time delta has no
 * codeword. Counting and writing share it so both passes agree. */
static int code_envelope(const SBRInfo *sbr, const SbrFrameData *fd, const int *cur, const int *ref,
                         BitAccumulator *acc, bool write)
{
    int amp = fd->eff_amp_res;
    int nb = sbr_env_bands(sbr, fd);
    const SBRHuffEntry *tab = amp ? f_huff_env_3_0dB : f_huff_env_1_5dB;
    int lav = amp ? F_HUFF_ENV_3_0DB_OFFSET : F_HUFF_ENV_1_5DB_OFFSET;
    int bits = 0, b = 0;

    if (ref) {
        tab = amp ? t_huff_env_3_0dB : t_huff_env_1_5dB;
        lav = T_HUFF_ENV_LAV;
    } else {
        bits = amp ? 6 : 7;
        if (write) AccumPutBits(acc, (uint32_t)cur[0], bits);
        b = 1;
    }
    for (; b < nb; b++) {
        int d = cur[b] - (ref ? ref[b] : cur[b - 1]);
        if (d < -lav || d > lav) return INT_MAX;
        if (write) AccumPutBits(acc, (uint32_t)tab[d + lav].code, tab[d + lav].len);
        bits += tab[d + lav].len;
    }
    return bits;
}

/* Each envelope takes the cheaper of frequency and time deltas; a frame's
 * first envelope refers to the channel's last written one, when linked. The
 * sizing pass (write false) makes the choice and caches it for the write pass,
 * which always follows it with the same frame. */
static int write_sbr_envelope(SBRInfo *sbr, const SbrFrameData *fd, int linked, BitStream *bs, int ch, bool write)
{
    SBRChannel *sc = &sbr->ch[ch];
    unsigned chosen = 0;
    int bits = 0;
    BitAccumulator acc = {0};

    if (write) AccumBegin(&acc, bs);
    for (int e = 0; e < fd->numEnvelopes; e++) {
        const int *ref = e ? fd->ch[ch].envData[e - 1] : sc->ref[~sbr->frameCount & 1].env;
        int first = write ? (sc->envDt >> e) & 1 : 0;
        int last = write ? first : (e || linked);
        int best = INT_MAX;
        for (int t = first; t <= last; t++) {
            int n = code_envelope(sbr, fd, fd->ch[ch].envData[e], t ? ref : NULL, &acc, write);
            if (n < best) { best = n; chosen = (chosen & ~(1u << e)) | ((unsigned)t << e); }
        }
        bits += best;
    }
    if (write) AccumEnd(&acc);
    else sc->envDt = chosen;
    return bits;
}

/* One noise band at a constant level: 5-bit absolute, or the time delta 0,
 * whose t_huffman_noise_3_0dB code is the single bit 0 -- always the cheaper
 * once a reference exists. */
static int write_sbr_noise(const SbrFrameData *fd, int noiseLinked, BitStream *bs, bool write)
{
    int n_q = fd->numEnvelopes > 1 ? 2 : 1;
    int bits = 0;
    for (int q = 0; q < n_q; q++) {
        int len = (q || noiseLinked) ? 1 : 5;
        if (write) PutBit(bs, len == 1 ? 0 : SBR_NOISE_LEVEL_DEFAULT, len);
        bits += len;
    }
    return bits;
}

static int write_sbr_data(SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs, int id_aac, int ch0, int sendHeader, bool write)
{
    int nch = (id_aac == ID_CPE) ? 2 : 1;
    int flags_len = (id_aac == ID_CPE) ? 3 : 2;
    int lead_len = (id_aac == ID_CPE) ? 2 : 1;
    int bits = lead_len + flags_len;
    /* Time deltas need the previous frame's envelope in the same layout;
     * header frames stay self-contained so a decoder can start there. */
    const SBRChannel *sc0 = &sbr->ch[ch0];
    const SbrEnvRef *prev = &sc0->ref[~sbr->frameCount & 1];
    int noiseLinked = !sendHeader && prev->nb;
    int linked = noiseLinked && prev->nb == sbr_env_bands(sbr, fd) && prev->ampRes == fd->eff_amp_res;

    if (write) PutBit(bs, 0, lead_len); /* bs_coupling / reserved */

    for (int ch = 0; ch < nch; ch++)
        bits += write_sbr_grid(sbr, fd, bs, write);
    for (int ch = 0; ch < nch; ch++)
        bits += write_sbr_dtdf(&sbr->ch[ch0 + ch], fd, noiseLinked, bs, write);
    for (int ch = 0; ch < nch; ch++)
        bits += write_sbr_invf(bs, write);
    for (int ch = 0; ch < nch; ch++)
        bits += write_sbr_envelope(sbr, fd, linked, bs, ch0 + ch, write);
    for (int ch = 0; ch < nch; ch++)
        bits += write_sbr_noise(fd, noiseLinked, bs, write);

    if (write) PutBit(bs, 0, flags_len); /* add_harmonic / extended data flags */

    /* Only the real write records the reference, so the sizing pass makes
     * the same choices. */
    if (write) {
        int nb = sbr_env_bands(sbr, fd);
        for (int ch = 0; ch < nch; ch++) {
            SbrEnvRef *cur = &sbr->ch[ch0 + ch].ref[sbr->frameCount & 1];
            memcpy(cur->env, fd->ch[ch0 + ch].envData[fd->numEnvelopes - 1], nb * sizeof(int));
            cur->nb = nb;
            cur->ampRes = fd->eff_amp_res;
        }
    }

    return bits;
}

/* Emit the full extension_payload body for EXT_SBR_DATA: the 4-bit extension
 * type, the 1-bit header flag, the optional header, and the channel data. */
static int emit_sbr_payload(SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs, int id_aac, int ch0, int sendHeader, bool write)
{
    int bits = 5;
    if (write) PutBit(bs, (SBR_EXT_TYPE_SBR << 1) | (sendHeader & 1), 5);
    if (sendHeader) bits += write_sbr_header(sbr, bs, write);
    bits += write_sbr_data(sbr, fd, bs, id_aac, ch0, sendHeader, write);
    return bits;
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
    int payloadBits = emit_sbr_payload(sbr, fd, NULL, id_aac, ch0, sendHeader, false);
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
    emit_sbr_payload(sbr, fd, bs, id_aac, ch0, sendHeader, true);
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
