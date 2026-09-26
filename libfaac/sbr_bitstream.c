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
        /* ISO 14496-3:2009 §4.6.18.5 sbr_header() (21 bits) */
        PutBit(bs, sbr->inject ? sbr->bs_amp_res : SBR_AMP_RES, 1);
        PutBit(bs, sbr->bs_start_freq,  4); /* bs_start_freq: crossover index */
        PutBit(bs, sbr->bs_stop_freq,   4); /* bs_stop_freq: high-band ceil */
        PutBit(bs, sbr->bs_xover_band,  3); /* bs_xover_band: low-res split (0=none) */
        PutBit(bs, 0,                   2); /* bs_reserved */
        PutBit(bs, 1,                   1); /* bs_header_extra_1 = 1 */
        PutBit(bs, 0,                   1); /* bs_header_extra_2 = 0 */
        PutBit(bs, sbr->bs_freq_scale,  2);
        PutBit(bs, sbr->bs_alter_scale, 1);
        PutBit(bs, sbr->inject ? sbr->bs_noise_bands : 0, 2);
    }
    return 21;
}

/* Width of the transient pointer field, indexed by number of envelopes. */
static const int sbr_ceil_log2[] = { 0, 1, 2, 2, 3, 3 };

static int write_sbr_grid(const SBRInfo *sbr, const SbrGrid *g, BitStream *bs, bool write)
{
    int num_env = g->numEnvelopes;
    int bits = 2;

    if (write) PutBit(bs, g->frameClass, 2);
    if (g->frameClass == SBR_FRAME_CLASS_FIXVAR) {
        if(write){ PutBit(bs,g->tEnv[num_env]-16,2); PutBit(bs,num_env-1,2); for(int i=0;i<num_env-1;i++) PutBit(bs,(g->tEnv[num_env-i]-g->tEnv[num_env-i-1]-2)/2,2); PutBit(bs,g->bsPointer,sbr_ceil_log2[num_env]); for(int i=num_env-1;i>=0;i--) PutBit(bs,g->freqResEnv[i],1); }
        bits += 4 + 2*(num_env-1) + sbr_ceil_log2[num_env] + num_env;
    } else if (g->frameClass == SBR_FRAME_CLASS_VARFIX) {
        /* VARFIX (§4.6.18.3.6): variable leading borders, fixed (untransmitted)
         * trailing border at numTimeSlots, then bs_pointer and per-envelope
         * bs_freq_res. */
        if (write) {
            PutBit(bs, g->tEnv[0], 2);                  /* bs_var_bord_0 */
            PutBit(bs, num_env - 1, 2);                  /* bs_num_rel_0   */
            for (int i = 0; i < num_env - 1; i++)
                PutBit(bs, (g->tEnv[i + 1] - g->tEnv[i] - 2) / 2, 2); /* bs_rel_bord */
        }
        int ptr_len = sbr_ceil_log2[num_env];
        if (write) {
            PutBit(bs, g->bsPointer, ptr_len);
            for (int i = 0; i < num_env; i++)
                PutBit(bs, sbr->inject ? g->freqResEnv[i] : sbr->bs_freq_res, 1);
        }
        bits += 4 + 2 * (num_env - 1) + ptr_len + num_env;
    } else if (g->frameClass == SBR_FRAME_CLASS_VARVAR) {
        /* tEnv alone does not identify the VARVAR split: a border before 16
           can be represented either from the leading or trailing side. Pick
           the split whose relative borders are all legal 2,4,6,8-slot
           syntax values. This is essential for FDK grids such as 3,6,10,16. */
        int n0 = 0, n1 = num_env - 1;
        for (int split = 0; split < num_env; split++) {
            int ok = 1;
            for (int i = 0; i < split; i++) { int d = g->tEnv[i + 1] - g->tEnv[i]; if (d < 2 || d > 8 || (d & 1)) ok = 0; }
            for (int i = 0; i < num_env - 1 - split; i++) { int d = g->tEnv[num_env - i] - g->tEnv[num_env - i - 1]; if (d < 2 || d > 8 || (d & 1)) ok = 0; }
            if (ok) { n0 = split; n1 = num_env - 1 - split; break; }
        }
        if(write){PutBit(bs,g->tEnv[0],2);PutBit(bs,g->tEnv[num_env]-16,2);PutBit(bs,n0,2);PutBit(bs,n1,2);for(int i=0;i<n0;i++)PutBit(bs,(g->tEnv[i+1]-g->tEnv[i]-2)/2,2);for(int i=0;i<n1;i++)PutBit(bs,(g->tEnv[num_env-i]-g->tEnv[num_env-i-1]-2)/2,2);PutBit(bs,g->bsPointer,sbr_ceil_log2[num_env]);for(int i=0;i<num_env;i++)PutBit(bs,g->freqResEnv[i],1);}
        bits += 8+2*(num_env-1)+sbr_ceil_log2[num_env]+num_env;
    } else {
        /* FIXFIX: equal-spaced borders (not transmitted, the decoder derives
         * them from the envelope count), one bs_freq_res for all envelopes. */
        if (write) {
            PutBit(bs, num_env > 1 ? 1 : 0, 2);         /* bs_num_env = 1 << this */
            PutBit(bs, sbr->inject ? g->freqResEnv[0] : sbr->bs_freq_res, 1);
        }
        bits += 3;
    }
    return bits;
}

static int write_sbr_dtdf(const SbrGrid *g, BitStream *bs, bool write)
{
    int n_q = g->numEnvelopes > 1 ? 2 : 1;
    int len = g->numEnvelopes + n_q;
    if (write) PutBit(bs, 0, len);
    return len;
}

static int write_sbr_invf(const SBRInfo *sbr, const SbrFrameData *fd, int ch, BitStream *bs, bool write)
{
    if (!sbr->inject) { if (write) PutBit(bs, SBR_INVF_MODE, 2); return 2; }
    int n = sbr->bs_noise_bands ? sbr->numBandsLow : 1;
    for (int k=0;k<n;k++) if (write) PutBit(bs, fd->ch[ch].invfMode[k], 2);
    return 2*n;
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
    const SbrGrid *g = &fd->grid[ch];
    const SBRHuffEntry *table = g->eff_amp_res ? f_huff_env_3_0dB : f_huff_env_1_5dB;
    int nsyms = g->eff_amp_res ? F_HUFF_ENV_3_0DB_NSYMS : F_HUFF_ENV_1_5DB_NSYMS;
    int offset = g->eff_amp_res ? F_HUFF_ENV_3_0DB_OFFSET : F_HUFF_ENV_1_5DB_OFFSET;
    int first_bits = g->eff_amp_res ? 6 : 7;
    int first_max = (1 << first_bits) - 1;
    int bits = 0;
    BitAccumulator acc = {0};

    if (write) AccumBegin(&acc, bs);
    for (int e = 0; e < g->numEnvelopes; e++) {
        int nb = sbr->inject ? sbr_env_bands_at(sbr, g, e) : sbr_env_bands(sbr, g);
        const int *env_ch = fd->ch[ch].envData[e];
        if (write) AccumPutBits(&acc, (uint32_t)clamp_int(env_ch[0], 0, first_max), first_bits);
        bits += first_bits;
        for (int b = 1; b < nb; b++)
            bits += put_huff(&acc, write, table, nsyms, offset, env_ch[b]);
    }
    if (write) AccumEnd(&acc);
    return bits;
}

static int write_sbr_noise(const SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs, int ch, bool write)
{
    int n_q = fd->grid[ch].numEnvelopes > 1 ? 2 : 1;
    if (!sbr->inject) { if(write) for(int ne=0;ne<n_q;ne++) PutBit(bs,SBR_NOISE_LEVEL_DEFAULT,5); return n_q*5; }
    int nb = sbr->bs_noise_bands ? sbr->numBandsLow : 1;
    BitAccumulator a={0}; if(write) AccumBegin(&a,bs);
    int bits=0;
    for (int ne = 0; ne < n_q; ne++) {
        if(write) AccumPutBits(&a, fd->ch[ch].noiseData[ne][0], 5); bits += 5;
        for(int k=1;k<nb;k++) bits += put_huff(&a,write,f_huff_env_3_0dB,F_HUFF_ENV_3_0DB_NSYMS,F_HUFF_ENV_3_0DB_OFFSET,fd->ch[ch].noiseData[ne][k]-fd->ch[ch].noiseData[ne][k-1]);
    }
    if(write) AccumEnd(&a);
    return bits;
}

static int write_sbr_data(const SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs, int id_aac, int ch0, bool write)
{
    int nch = (id_aac == ID_CPE) ? 2 : 1;
    int flags_len = (id_aac == ID_CPE) ? 3 : 2;
    int lead_len = (id_aac == ID_CPE) ? 2 : 1;
    int bits = lead_len + flags_len;

    if (write) PutBit(bs, 0, lead_len); /* bs_coupling / reserved */

    for (int ch = 0; ch < nch; ch++)
        bits += write_sbr_grid(sbr, &fd->grid[ch0 + ch], bs, write);
    for (int ch = 0; ch < nch; ch++)
        bits += write_sbr_dtdf(&fd->grid[ch0 + ch], bs, write);
    for (int ch = 0; ch < nch; ch++)
        bits += write_sbr_invf(sbr, fd, ch0 + ch, bs, write);
    for (int ch = 0; ch < nch; ch++)
        bits += write_sbr_envelope(sbr, fd, bs, ch0 + ch, write);
    for (int ch = 0; ch < nch; ch++)
        bits += write_sbr_noise(sbr, fd, bs, ch0 + ch, write);

    if (!sbr->inject) { if(write) PutBit(bs,0,flags_len); }
    else { bits -= flags_len; for (int ch=0;ch<nch;ch++) { if(write) { PutBit(bs, fd->ch[ch0+ch].addHarmonicFlag,1); if(fd->ch[ch0+ch].addHarmonicFlag) for(int k=0;k<sbr->numBands;k++) PutBit(bs,fd->ch[ch0+ch].addHarmonic[k],1); } bits += 1 + (fd->ch[ch0+ch].addHarmonicFlag?sbr->numBands:0); } if (write) PutBit(bs, 0, 1); bits++; }

    return bits;
}

/* Emit the full extension_payload body for EXT_SBR_DATA: the 4-bit extension
 * type, the 1-bit header flag, the optional header, and the channel data. */
static int emit_sbr_payload(const SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs, int id_aac, int ch0, int sendHeader, bool write)
{
    int bits = 5;
    if (write) PutBit(bs, (SBR_EXT_TYPE_SBR << 1) | (sendHeader & 1), 5);
    if (sendHeader) bits += write_sbr_header(sbr, bs, write);
    bits += write_sbr_data(sbr, fd, bs, id_aac, ch0, write);
    return bits;
}

static int SbrWrite(const SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs, int id_aac, int ch0)
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
