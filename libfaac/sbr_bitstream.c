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
        uint32_t w = ((uint32_t)sbr->bs_amp_res << 20) |
                     ((uint32_t)sbr->bs_start_freq << 16) |
                     ((uint32_t)sbr->bs_stop_freq << 12) |
                     ((uint32_t)sbr->bs_xover_band << 9) |
                     (1U << 6) |
                     ((uint32_t)sbr->bs_alter_scale << 2);
        PutBit(bs, w, 21);
    }
    return 21;
}

/* Width of the transient pointer field, indexed by number of envelopes. */
static const int sbr_ceil_log2[] = { 0, 1, 2, 2, 3, 3 };

static int write_sbr_grid(const SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs, bool write)
{
    int num_env = fd->numEnvelopes;
    int bits;
    if (fd->frameClass == SBR_FRAME_CLASS_VARFIX) {
        bits = 6 + (num_env - 1) * 2 + sbr_ceil_log2[num_env] + num_env;
        if (write) {
            uint32_t w = (SBR_FRAME_CLASS_VARFIX << 4) | (fd->tEnv[0] << 2) | (num_env - 1);
            int n = 6;
            for (int i = 0; i < num_env - 1; i++) {
                w = (w << 2) | ((fd->tEnv[i + 1] - fd->tEnv[i] - 2) / 2);
                n += 2;
            }
            w = (w << sbr_ceil_log2[num_env]) | fd->bsPointer;
            n += sbr_ceil_log2[num_env];
            for (int i = 0; i < num_env; i++) {
                w = (w << 1) | sbr->bs_freq_res;
                n += 1;
            }
            PutBit(bs, w, n);
        }
    } else {
        bits = 5;
        if (write) {
            uint32_t w = (SBR_FRAME_CLASS_FIXFIX << 3) | ((num_env > 1 ? 1 : 0) << 1) | sbr->bs_freq_res;
            PutBit(bs, w, 5);
        }
    }
    return bits;
}

static int write_sbr_dtdf(const SbrFrameData *fd, BitStream *bs, bool write)
{
    int n_q = fd->numEnvelopes > 1 ? 2 : 1;
    int bits = fd->numEnvelopes + n_q;
    if (write) PutBit(bs, 0, bits);
    return bits;
}

static int write_sbr_invf(const SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs, int ch, bool write)
{
    if (write) {
        for (int nb = 0; nb < sbr->numNoiseBands; nb++)
            PutBit(bs, fd->ch[ch].invfMode, 2);
    }
    return sbr->numNoiseBands * 2;
}

static int write_sbr_envelope(const SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs, int ch, bool write)
{
    const SBRHuffEntry *table = fd->eff_amp_res ? f_huff_env_3_0dB : f_huff_env_1_5dB;
    int nsyms = fd->eff_amp_res ? F_HUFF_ENV_3_0DB_NSYMS : F_HUFF_ENV_1_5DB_NSYMS;
    int offset = fd->eff_amp_res ? F_HUFF_ENV_3_0DB_OFFSET : F_HUFF_ENV_1_5DB_OFFSET;
    int first_bits = fd->eff_amp_res ? 6 : 7;
    int first_max = (1 << first_bits) - 1;
    int bits = 0;

    if (write) {
        BitAccumulator acc = {0};
        AccumBegin(&acc, bs);
        for (int e = 0; e < fd->numEnvelopes; e++) {
            const int *env_ch = fd->ch[ch].envData[e];
            AccumPutBits(&acc, clamp_int(env_ch[0], 0, first_max), first_bits);
            bits += first_bits;
            for (int b = 1; b < sbr->numBands; b++) {
                int sym = clamp_int(env_ch[b] + offset, 0, nsyms - 1);
                AccumPutBits(&acc, table[sym].code, table[sym].len);
                bits += table[sym].len;
            }
        }
        AccumEnd(&acc);
    } else {
        for (int e = 0; e < fd->numEnvelopes; e++) {
            const int *env_ch = fd->ch[ch].envData[e];
            bits += first_bits;
            for (int b = 1; b < sbr->numBands; b++) {
                int sym = clamp_int(env_ch[b] + offset, 0, nsyms - 1);
                bits += table[sym].len;
            }
        }
    }
    return bits;
}

static int write_sbr_noise(const SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs, int ch, bool write)
{
    int n_q = fd->numEnvelopes > 1 ? 2 : 1;
    int bits = 0;

    if (write) {
        BitAccumulator acc = {0};
        AccumBegin(&acc, bs);
        for (int ne = 0; ne < n_q; ne++) {
            for (int nb = 0; nb < sbr->numNoiseBands; nb++) {
                int val = fd->ch[ch].noiseData[ne][nb];
                if (nb == 0) {
                    AccumPutBits(&acc, clamp_int(val, 0, 30), 5);
                    bits += 5;
                } else {
                    int sym = clamp_int(val + F_HUFF_ENV_3_0DB_OFFSET, 0, F_HUFF_ENV_3_0DB_NSYMS - 1);
                    AccumPutBits(&acc, f_huff_env_3_0dB[sym].code, f_huff_env_3_0dB[sym].len);
                    bits += f_huff_env_3_0dB[sym].len;
                }
            }
        }
        AccumEnd(&acc);
    } else {
        for (int ne = 0; ne < n_q; ne++) {
            for (int nb = 0; nb < sbr->numNoiseBands; nb++) {
                int val = fd->ch[ch].noiseData[ne][nb];
                if (nb == 0) {
                    bits += 5;
                } else {
                    int sym = clamp_int(val + F_HUFF_ENV_3_0DB_OFFSET, 0, F_HUFF_ENV_3_0DB_NSYMS - 1);
                    bits += f_huff_env_3_0dB[sym].len;
                }
            }
        }
    }
    return bits;
}

static int write_sbr_data(const SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs, int id_aac, bool write)
{
    int nch = (id_aac == ID_CPE) ? 2 : 1;
    int flags_len = (id_aac == ID_CPE) ? 3 : 2;
    int lead_len = (id_aac == ID_CPE) ? 2 : 1;
    int bits = lead_len + flags_len;

    if (write) PutBit(bs, 0, lead_len);

    for (int ch = 0; ch < nch; ch++)
        bits += write_sbr_grid(sbr, fd, bs, write);
    for (int ch = 0; ch < nch; ch++)
        bits += write_sbr_dtdf(fd, bs, write);
    for (int ch = 0; ch < nch; ch++)
        bits += write_sbr_invf(sbr, fd, bs, ch, write);
    for (int ch = 0; ch < nch; ch++)
        bits += write_sbr_envelope(sbr, fd, bs, ch, write);
    for (int ch = 0; ch < nch; ch++)
        bits += write_sbr_noise(sbr, fd, bs, ch, write);

    if (write) PutBit(bs, 0, flags_len);

    return bits;
}

/* Emit the full extension_payload body for EXT_SBR_DATA: the 4-bit extension
 * type, the 1-bit header flag, the optional header, and the channel data. */
static int emit_sbr_payload(SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs, int id_aac, int sendHeader, bool write)
{
    if (write) PutBit(bs, (SBR_EXT_TYPE_SBR << 1) | (sendHeader & 1), 5);
    int bits = 5;

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
        if (fillBytes < 15) {
            PutBit(bs, (ID_FIL << 4) | fillBytes, 7);
            totalBits = 7;
        } else {
            PutBit(bs, (ID_FIL << 12) | (15 << 8) | (fillBytes - 14), 15);
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
