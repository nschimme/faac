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

#include "sbr.h"
#include "sbr_internal.h"
#include "sbr_tables.h"
#include "bitstream.h"
#include "channels.h"
#include "util.h"
#include "faac_internal.h"

static int write_sbr_header(const SBRInfo *sbr, BitStream *bs, bool write)
{
    /* ISO 14496-3:2009 §4.6.18.5 sbr_header() (21 bits) */
    if (write) {
        PutBit(bs, sbr->bs_amp_res,     1); /* bs_amp_res: 0=1.5dB, 1=3dB */
        PutBit(bs, sbr->bs_start_freq,  4); /* bs_start_freq: crossover index */
        PutBit(bs, sbr->bs_stop_freq,   4); /* bs_stop_freq: high-band ceil */
        PutBit(bs, sbr->bs_xover_band,  3); /* bs_xover_band: low-res split (0=none) */
        PutBit(bs, 0,                   2); /* bs_reserved */
        PutBit(bs, 1,                   1); /* bs_header_extra_1 = 1 */
        PutBit(bs, 0,                   1); /* bs_header_extra_2 = 0 */
        PutBit(bs, 0,                   2); /* bs_freq_scale = 0 */
        PutBit(bs, sbr->bs_alter_scale, 1);
        PutBit(bs, 0,                   2); /* bs_noise_bands = 0 */
    }
    return 21;
}

/* Width of the transient pointer field, indexed by number of envelopes. */
static const int sbr_ceil_log2[] = { 0, 1, 2, 2, 3, 3 };

static int write_sbr_grid(const SbrFrameData *fd, BitStream *bs, bool write)
{
    int num_env = fd->numEnvelopes;
    int bits = 2;

    if (write) PutBit(bs, fd->frameClass, 2);
    if (fd->frameClass == SBR_FRAME_CLASS_FIXFIX) {
        /* FIXFIX: equal-spaced borders (not transmitted, the decoder derives
         * them from the envelope count), one bs_freq_res per envelope. */
        if (write) {
            int ld = 0;                                  /* bs_num_env_raw = log2 */
            while ((1 << ld) < num_env) ld++;
            PutBit(bs, ld, 2);
            PutBit(bs, fd->freqRes, 1);
        }
        bits += 3;
    } else {
        if (fd->frameClass == SBR_FRAME_CLASS_VARFIX) {
            /* VARFIX (§4.6.18.3.6): variable leading borders, fixed (untransmitted)
             * trailing border at numTimeSlots. */
            if (write) {
                PutBit(bs, fd->tEnv[0], 2);                  /* bs_var_bord_0 */
                PutBit(bs, num_env - 1, 2);                  /* bs_num_rel_0   */
                for (int i = 0; i < num_env - 1; i++)
                    PutBit(bs, (fd->tEnv[i + 1] - fd->tEnv[i] - 2) / 2, 2); /* bs_rel_bord */
            }
        } else {
            /* FIXVAR: fixed leading border, variable trailing borders -- the
             * mirror of VARFIX, walked from the trailing edge backwards. */
            if (write) {
                PutBit(bs, fd->tEnv[num_env] - SBR_NUM_TIME_SLOTS, 2);  /* bs_var_bord_1 */
                PutBit(bs, num_env - 1, 2);                             /* bs_num_rel_1  */
                for (int i = 0; i < num_env - 1; i++)
                    PutBit(bs, (fd->tEnv[num_env - i] - fd->tEnv[num_env - 1 - i] - 2) / 2, 2);
            }
        }
        /* Shared tail for both variable-grid classes: bs_pointer names the
         * transient envelope, then one bs_freq_res per envelope. */
        int ptr_len = sbr_ceil_log2[num_env];
        if (write) {
            PutBit(bs, fd->bsPointer, ptr_len);
            for (int i = 0; i < num_env; i++)
                PutBit(bs, fd->freqRes, 1);
        }
        bits += 4 + 2 * (num_env - 1) + ptr_len + num_env;
    }
    return bits;
}

static int write_sbr_dtdf(const SbrFrameData *fd, BitStream *bs, bool write)
{
    int n_q = fd->numEnvelopes > 1 ? 2 : 1;
    int len = fd->numEnvelopes + n_q;
    if (write) PutBit(bs, 0, len);
    return len;
}

static int write_sbr_invf(const SbrFrameData *fd, BitStream *bs, int ch, bool write)
{
    if (write) PutBit(bs, fd->ch[ch].invfMode, 2);
    return 2;
}

/* count-and-write helper, matching channels.c's WriteElement/WriteICS style. */
static int put_huff(BitAccumulator *acc, bool write, const SBRHuffEntry *table, int nsyms, int offset, int delta)
{
    int sym = clamp_int(delta + offset, 0, nsyms - 1);
    if (write) AccumPutBits(acc, (uint32_t)table[sym].code, table[sym].len);
    return table[sym].len;
}

/* Same shape as writesf()'s per-band loop, so it gets the same BitAccumulator batching. */
static int write_sbr_envelope(const SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs, int ch, bool write)
{
    const SBRHuffEntry *table = fd->eff_amp_res ? f_huff_env_3_0dB : f_huff_env_1_5dB;
    int nsyms = fd->eff_amp_res ? F_HUFF_ENV_3_0DB_NSYMS : F_HUFF_ENV_1_5DB_NSYMS;
    int offset = fd->eff_amp_res ? F_HUFF_ENV_3_0DB_OFFSET : F_HUFF_ENV_1_5DB_OFFSET;
    int first_bits = fd->eff_amp_res ? 6 : 7;
    int first_max = (1 << first_bits) - 1;
    int nb = sbr_env_bands(sbr, fd);
    int bits = 0;
    BitAccumulator acc = {0};

    if (write) AccumBegin(&acc, bs);
    for (int e = 0; e < fd->numEnvelopes; e++) {
        const int *env_ch = fd->ch[ch].envData[e];
        if (write) AccumPutBits(&acc, (uint32_t)clamp_int(env_ch[0], 0, first_max), first_bits);
        bits += first_bits;
        for (int b = 1; b < nb; b++) {
            bits += put_huff(&acc, write, table, nsyms, offset, env_ch[b]);
        }
    }
    if (write) AccumEnd(&acc);
    return bits;
}

static int write_sbr_noise(const SbrFrameData *fd, BitStream *bs, int ch, bool write)
{
    int n_q = fd->numEnvelopes > 1 ? 2 : 1;
    if (write) {
        for (int ne = 0; ne < n_q; ne++)
            PutBit(bs, clamp_int(fd->ch[ch].noiseData[ne][0], 0, 30), 5);
    }
    return n_q * 5;
}

static int write_sbr_data(const SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs, int id_aac, bool write)
{
    int nch = (id_aac == ID_CPE) ? 2 : 1;
    int flags_len = (id_aac == ID_CPE) ? 3 : 2;
    int lead_len = (id_aac == ID_CPE) ? 2 : 1;
    int bits = lead_len + flags_len;

    if (write) PutBit(bs, 0, lead_len);

    for (int ch = 0; ch < nch; ch++)
        bits += write_sbr_grid(fd, bs, write);
    for (int ch = 0; ch < nch; ch++)
        bits += write_sbr_dtdf(fd, bs, write);
    for (int ch = 0; ch < nch; ch++)
        bits += write_sbr_invf(fd, bs, ch, write);
    for (int ch = 0; ch < nch; ch++)
        bits += write_sbr_envelope(sbr, fd, bs, ch, write);
    for (int ch = 0; ch < nch; ch++)
        bits += write_sbr_noise(fd, bs, ch, write);

    if (write) PutBit(bs, 0, flags_len);

    return bits;
}

/* Emit the full extension_payload body for EXT_SBR_DATA: the 4-bit extension
 * type, the 1-bit header flag, the optional header, and the channel data. */
static int emit_sbr_payload(SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs, int id_aac, int sendHeader, bool write)
{
    int bits = 5;
    if (write) PutBit(bs, (SBR_EXT_TYPE_SBR << 1) | (sendHeader & 1), 5);
    if (sendHeader) bits += write_sbr_header(sbr, bs, write);
    bits += write_sbr_data(sbr, fd, bs, id_aac, write);
    return bits;
}

int SbrWrite(SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs, int id_aac, int writeFlag)
{
    if (!sbr || !sbr->sbrPresent) return 0;

    int sendHeader = sbr->sendHeaderThisFrame;

    /* The fill_element's cnt field must precede the payload in the bitstream,
     * so its size is needed before anything is written. Re-deriving it with a
     * dry (write=false) pass is cheap -- a few hundred fixed-width/Huffman
     * fields, not a hot loop -- so there's no need to cache the emitted bits
     * across a frame's several SbrWrite calls (BuildFrame's count and write
     * passes, plus frame.c's rate-control bit-accounting call): every call
     * just re-derives them from sbr's already-quantized envelope/noise data,
     * the same way channels.c's WriteElement/WriteICS do for the rest of the
     * frame. */
    int payloadBits = emit_sbr_payload(sbr, fd, NULL, id_aac, sendHeader, false);
    int fillBytes = (payloadBits + 7) / 8;
    int padBits = fillBytes * 8 - payloadBits;

    /* The fill_element count escapes through an 8-bit field, so a single
     * extension_payload tops out at 15 + 255 - 1 = 269 bytes. A larger SBR
     * payload would silently truncate esc_count and corrupt the boundary. */
    assert(fillBytes <= 14 + 255);

    int totalBits;
    if (writeFlag) {
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
        emit_sbr_payload(sbr, fd, bs, id_aac, sendHeader, true);
        if (padBits > 0) PutBit(bs, 0, padBits);
        sbr->headerSent = 1;
        sbr->frameCount++;
    } else {
        totalBits = (fillBytes < 15) ? 7 : 15;
    }
    return totalBits + payloadBits + padBits;
}

int SbrContextGetBits(SBRContext *sCtx, BitStream *bs, int channels, int aacObjectType, int writeFlag)
{
    if (aacObjectType == HE_V1 && sCtx) {
        if (sCtx->sbrInfo) {
            int id_aac = (channels > 1) ? ID_CPE : ID_SCE;
            /* One step past the newest slot is the oldest: the payload whose
             * audio this access unit's core carries. See SBR_FRAME_FIFO. */
            const SbrFrameData *fd = &sCtx->frameFIFO[(sCtx->frameHead + 1) % SBR_FRAME_FIFO];
            return SbrWrite(sCtx->sbrInfo, fd, bs, id_aac, writeFlag);
        }
    }
    return 0;
}
