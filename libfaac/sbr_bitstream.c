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
    if (write) {
        PutBit(bs, sbr->bs_amp_res,     1);
        PutBit(bs, sbr->bs_start_freq,  4);
        PutBit(bs, sbr->bs_stop_freq,   4);
        PutBit(bs, sbr->bs_xover_band,  3);
        PutBit(bs, 0,                   2);
        PutBit(bs, 1,                   1);
        PutBit(bs, 0,                   1);
        PutBit(bs, 0,                   2);
        PutBit(bs, sbr->bs_alter_scale, 1);
        PutBit(bs, 0,                   2);
    }
    return 21;
}

/* Width of the transient pointer field, indexed by number of envelopes. */
static const uint8_t sbr_ceil_log2[] = { 0, 1, 2, 2, 3, 3 };

static int write_sbr_grid(const SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs, bool write)
{
    int num_env = fd->numEnvelopes;
    int bits = 2;

    if (write) PutBit(bs, fd->frameClass, 2);
    if (fd->frameClass == SBR_FRAME_CLASS_VARFIX) {
        if (write) {
            PutBit(bs, fd->tEnv[0], 2);
            PutBit(bs, num_env - 1, 2);
            for (int i = 0; i < num_env - 1; i++)
                PutBit(bs, (fd->tEnv[i + 1] - fd->tEnv[i] - 2) / 2, 2);
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
            PutBit(bs, num_env > 1 ? 1 : 0, 2);
            PutBit(bs, sbr->bs_freq_res, 1);
        }
        bits += 3;
    }
    return bits;
}

static inline int put_huff(BitAccumulator *acc, bool write, const SBRHuffEntry *table, int nsyms, int offset, int delta)
{
    int sym = clamp_int(delta + offset, 0, nsyms - 1);
    if (write) AccumPutBits(acc, (uint32_t)table[sym].code, table[sym].len);
    return table[sym].len;
}

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
        for (int b = 1; b < nb; b++)
            bits += put_huff(&acc, write, table, nsyms, offset, env_ch[b]);
    }
    if (write) AccumEnd(&acc);
    return bits;
}

static int write_sbr_data(const SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs, int id_aac, const int *ch_indices, bool write)
{
    int nch = (id_aac == ID_CPE) ? 2 : 1;
    int flags_len = (id_aac == ID_CPE) ? 3 : 2;
    int lead_len = (id_aac == ID_CPE) ? 2 : 1;
    int n_q = fd->numEnvelopes > 1 ? 2 : 1;
    int bits = lead_len + flags_len + nch * (fd->numEnvelopes + n_q + 2 + n_q * 5);

    if (write) PutBit(bs, 0, lead_len);

    for (int ch = 0; ch < nch; ch++)
        bits += write_sbr_grid(sbr, fd, bs, write);

    if (write) {
        for (int ch = 0; ch < nch; ch++)
            PutBit(bs, 0, fd->numEnvelopes + n_q);
        for (int ch = 0; ch < nch; ch++)
            PutBit(bs, fd->ch[ch_indices[ch]].invfMode, 2);
    }

    for (int ch = 0; ch < nch; ch++)
        bits += write_sbr_envelope(sbr, fd, bs, ch_indices[ch], write);

    if (write) {
        for (int ch = 0; ch < nch; ch++)
            for (int ne = 0; ne < n_q; ne++)
                PutBit(bs, clamp_int(fd->ch[ch_indices[ch]].noiseData[ne][0], 0, 30), 5);
        PutBit(bs, 0, flags_len);
    }

    return bits;
}

static int emit_sbr_payload(SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs, int id_aac, const int *ch_indices, int sendHeader, bool write)
{
    int bits = 5;
    if (write) PutBit(bs, (SBR_EXT_TYPE_SBR << 1) | (sendHeader & 1), 5);
    if (sendHeader) bits += write_sbr_header(sbr, bs, write);
    bits += write_sbr_data(sbr, fd, bs, id_aac, ch_indices, write);
    return bits;
}

int SbrWriteElement(SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs, int id_aac, const int *ch_indices, int writeFlag)
{
    if (!sbr || !sbr->sbrPresent) return 0;

    int sendHeader = sbr->sendHeaderThisFrame;
    int payloadBits = emit_sbr_payload(sbr, fd, NULL, id_aac, ch_indices, sendHeader, false);
    int fillBytes = (payloadBits + 7) / 8;
    int padBits = fillBytes * 8 - payloadBits;

    assert(fillBytes <= 14 + 255);

    int totalBits;
    if (writeFlag) {
        PutBit(bs, ID_FIL, 3);
        if (fillBytes < 15) {
            PutBit(bs, fillBytes, 4);
            totalBits = 7;
        } else {
            PutBit(bs, 15, 4);
            PutBit(bs, fillBytes - 14, 8);
            totalBits = 15;
        }
        emit_sbr_payload(sbr, fd, bs, id_aac, ch_indices, sendHeader, true);
        if (padBits > 0) PutBit(bs, 0, padBits);
        sbr->headerSent = 1;
    } else {
        totalBits = (fillBytes < 15) ? 7 : 15;
    }
    return totalBits + payloadBits + padBits;
}

int SbrContextGetBits(SBRContext *sCtx, BitStream *bs, const AACElement *elem, int aacObjectType, int writeFlag)
{
    if (aacObjectType == HE_V1 && sCtx && elem && elem->type != ID_LFE) {
        if (sCtx->sbrInfo) {
            int id_aac = (elem->type == ID_CPE) ? ID_CPE : ID_SCE;
            const SbrFrameData *fd = &sCtx->frameFIFO[(sCtx->frameHead + 1) % SBR_FRAME_FIFO];
            return SbrWriteElement(sCtx->sbrInfo, fd, bs, id_aac, elem->channels, writeFlag);
        }
    }
    return 0;
}
