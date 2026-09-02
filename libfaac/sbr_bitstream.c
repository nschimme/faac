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
    BitWriter bw;
    BitWriterInit(&bw);
    BitWriterPut(&bw, sbr->bs_amp_res,     1);
    BitWriterPut(&bw, sbr->bs_start_freq,  4);
    BitWriterPut(&bw, sbr->bs_stop_freq,   4);
    BitWriterPut(&bw, sbr->bs_xover_band,  3);
    BitWriterPut(&bw, 0,                   2); /* bs_reserved */
    BitWriterPut(&bw, 1,                   1); /* bs_header_extra_1 = 1 */
    BitWriterPut(&bw, 0,                   1); /* bs_header_extra_2 = 0 */
    BitWriterPut(&bw, 0,                   2); /* bs_freq_scale = 0 */
    BitWriterPut(&bw, sbr->bs_alter_scale, 1);
    BitWriterPut(&bw, 0,                   2); /* bs_noise_bands = 0 */
    return BitWriterFlush(&bw, bs, write);
}

/* Width of the transient pointer field, indexed by number of envelopes. */
static const int sbr_ceil_log2[] = { 0, 1, 2, 2, 3, 3 };

static int write_sbr_grid(const SbrFrameData *fd, BitStream *bs, bool write)
{
    int num_env = fd->numEnvelopes;
    BitWriter bw;
    BitWriterInit(&bw);

    BitWriterPut(&bw, fd->frameClass, 2);
    if (fd->frameClass == SBR_FRAME_CLASS_FIXFIX) {
        BitWriterPut(&bw, num_env > 1 ? 1 : 0, 2);
        BitWriterPut(&bw, fd->freqRes, 1);
    } else {
        if (fd->frameClass == SBR_FRAME_CLASS_VARFIX) {
            BitWriterPut(&bw, fd->tEnv[0], 2);                  /* bs_var_bord_0 */
            BitWriterPut(&bw, num_env - 1, 2);                  /* bs_num_rel_0   */
            for (int i = 0; i < num_env - 1; i++)
                BitWriterPut(&bw, (fd->tEnv[i + 1] - fd->tEnv[i] - 2) / 2, 2); /* bs_rel_bord */
        } else {
            BitWriterPut(&bw, fd->tEnv[num_env] - SBR_NUM_TIME_SLOTS, 2);  /* bs_var_bord_1 */
            BitWriterPut(&bw, num_env - 1, 2);                             /* bs_num_rel_1  */
            for (int i = 0; i < num_env - 1; i++)
                BitWriterPut(&bw, (fd->tEnv[num_env - i] - fd->tEnv[num_env - 1 - i] - 2) / 2, 2);
        }
        BitWriterPut(&bw, fd->bsPointer, sbr_ceil_log2[num_env]);
        for (int i = 0; i < num_env; i++)
            BitWriterPut(&bw, fd->freqRes, 1);
    }
    return BitWriterFlush(&bw, bs, write);
}

static int write_sbr_dtdf(const SbrFrameData *fd, BitStream *bs, bool write)
{
    BitWriter bw;
    BitWriterInit(&bw);
    int n_q = fd->numEnvelopes > 1 ? 2 : 1;
    BitWriterPut(&bw, 0, fd->numEnvelopes + n_q);
    return BitWriterFlush(&bw, bs, write);
}

static int write_sbr_invf(const SbrFrameData *fd, BitStream *bs, int ch, bool write)
{
    BitWriter bw;
    BitWriterInit(&bw);
    BitWriterPut(&bw, fd->ch[ch].invfMode, 2);
    return BitWriterFlush(&bw, bs, write);
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

    BitWriter bw;
    BitWriterInit(&bw);

    for (int e = 0; e < fd->numEnvelopes; e++) {
        const int *env_ch = fd->ch[ch].envData[e];
        if (bw.bits + first_bits > 32) {
            BitWriterFlush(&bw, bs, write);
            BitWriterInit(&bw);
        }
        BitWriterPut(&bw, clamp_int(env_ch[0], 0, first_max), first_bits);
        bits += first_bits;
        for (int b = 1; b < nb; b++) {
            int sym = clamp_int(env_ch[b] + offset, 0, nsyms - 1);
            int len = table[sym].len;
            if (bw.bits + len > 32) {
                BitWriterFlush(&bw, bs, write);
                BitWriterInit(&bw);
            }
            BitWriterPut(&bw, table[sym].code, len);
            bits += len;
        }
    }
    BitWriterFlush(&bw, bs, write);
    return bits;
}

static int write_sbr_noise(const SbrFrameData *fd, BitStream *bs, int ch, bool write)
{
    BitWriter bw;
    BitWriterInit(&bw);
    int n_q = fd->numEnvelopes > 1 ? 2 : 1;
    for (int ne = 0; ne < n_q; ne++)
        BitWriterPut(&bw, clamp_int(fd->ch[ch].noiseData[ne][0], 0, 30), 5);
    return BitWriterFlush(&bw, bs, write);
}

static int write_sbr_data(const SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs, int id_aac, bool write)
{
    int nch = (id_aac == ID_CPE) ? 2 : 1;
    int flags_len = (id_aac == ID_CPE) ? 3 : 2;
    int lead_len = (id_aac == ID_CPE) ? 2 : 1;
    int bits = lead_len + flags_len;

    BitWriter bw;
    BitWriterInit(&bw);
    BitWriterPut(&bw, 0, lead_len);
    BitWriterFlush(&bw, bs, write);

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

    BitWriterInit(&bw);
    BitWriterPut(&bw, 0, flags_len);
    BitWriterFlush(&bw, bs, write);

    return bits;
}

/* Emit the full extension_payload body for EXT_SBR_DATA: the 4-bit extension
 * type, the 1-bit header flag, the optional header, and the channel data. */
static int emit_sbr_payload(SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs, int id_aac, int sendHeader, bool write)
{
    BitWriter bw;
    BitWriterInit(&bw);
    BitWriterPut(&bw, (SBR_EXT_TYPE_SBR << 1) | (sendHeader & 1), 5);
    int bits = BitWriterFlush(&bw, bs, write);
    if (sendHeader) bits += write_sbr_header(sbr, bs, write);
    bits += write_sbr_data(sbr, fd, bs, id_aac, write);
    return bits;
}

int SbrWrite(SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs, int id_aac, int writeFlag)
{
    if (!sbr || !sbr->sbrPresent) return 0;

    int sendHeader = sbr->sendHeaderThisFrame;
    int payloadBits = emit_sbr_payload(sbr, fd, NULL, id_aac, sendHeader, false);
    int fillBytes = (payloadBits + 7) / 8;
    int padBits = fillBytes * 8 - payloadBits;

    assert(fillBytes <= 14 + 255);

    int totalBits;
    if (writeFlag) {
        BitWriter bw;
        BitWriterInit(&bw);
        BitWriterPut(&bw, ID_FIL, 3);
        if (fillBytes < 15) {
            BitWriterPut(&bw, fillBytes, 4);
            totalBits = 7;
        } else {
            BitWriterPut(&bw, 15, 4);
            BitWriterPut(&bw, fillBytes - 14, 8);
            totalBits = 15;
        }
        BitWriterFlush(&bw, bs, true);

        emit_sbr_payload(sbr, fd, bs, id_aac, sendHeader, true);

        if (padBits > 0) {
            BitWriterInit(&bw);
            BitWriterPut(&bw, 0, padBits);
            BitWriterFlush(&bw, bs, true);
        }
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
