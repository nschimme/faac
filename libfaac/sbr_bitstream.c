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

/* count-and-write helper: matches channels.c's WriteElement/WriteICS style --
 * every write_sbr_* function below takes a `write` flag, always returns the
 * bit count, and only touches `bs` (which may be NULL) when `write` is set. */
static int put_huff(BitStream *bs, bool write, const SBRHuffEntry *table, int nsyms, int offset, int delta)
{
    int sym = clamp_int(delta + offset, 0, nsyms - 1);
    if (write) PutBit(bs, table[sym].code, table[sym].len);
    return table[sym].len;
}

static int write_sbr_header(const SBRInfo *sbr, BitStream *bs, bool write)
{
    int bits = 0;
#define WB(v,n) do { if (write) PutBit(bs,(v),(n)); bits += (n); } while(0)
    /* ISO 14496-3:2009 §4.6.18.5 sbr_header() (21 bits) */
    WB(sbr->bs_amp_res,     1); /* bs_amp_res: 0=1.5dB, 1=3dB */
    WB(sbr->bs_start_freq,  4); /* bs_start_freq: crossover index */
    WB(sbr->bs_stop_freq,   4); /* bs_stop_freq: high-band ceil */
    WB(sbr->bs_xover_band,  3); /* bs_xover_band: low-res split (0=none) */
    WB(0,                   2); /* bs_reserved */
    WB(1,                   1); /* bs_header_extra_1 = 1 (alter_scale present) */
    WB(0,                   1); /* bs_header_extra_2 = 0 (limiter fields absent) */
    /* bs_header_extra_1 fields: */
    WB(0,                   2); /* bs_freq_scale = 0 (linear master table) */
    WB(sbr->bs_alter_scale, 1); /* bs_alter_scale: 1=coarser at low bitrate */
    WB(0,                   2); /* bs_noise_bands = 0 (→ 1 noise band) */
#undef WB
    return bits;
}

/* Width of the transient pointer field, indexed by number of envelopes. */
static const int sbr_ceil_log2[] = { 0, 1, 2, 2, 3, 3 };

static int write_sbr_grid(const SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs, bool write)
{
    int bits = 0;
#define WB(v,n) do { if (write) PutBit(bs,(v),(n)); bits += (n); } while(0)
    if (fd->frameClass == SBR_FRAME_CLASS_VARFIX) {
        /* VARFIX: variable leading borders, fixed trailing border. Mirrors the
         * inverse of FFmpeg read_sbr_grid()'s VARFIX case: t_env[0]=bs_var_bord_0,
         * each lead border adds 2*bs_rel+2, the trailing border is numTimeSlots
         * (not transmitted), then bs_pointer and per-envelope bs_freq_res. */
        int num_env = fd->numEnvelopes;
        WB(SBR_FRAME_CLASS_VARFIX, 2);
        WB(fd->tEnv[0], 2);                 /* bs_var_bord_0 */
        WB(num_env - 1, 2);                  /* bs_num_rel_0   */
        for (int i = 0; i < num_env - 1; i++)
            WB((fd->tEnv[i + 1] - fd->tEnv[i] - 2) / 2, 2); /* bs_rel_bord */
        WB(fd->bsPointer, sbr_ceil_log2[num_env]);
        for (int i = 0; i < num_env; i++)    /* bs_freq_res[1..num_env] */
            WB(sbr->bs_freq_res, 1);
    } else {
        /* FIXFIX: equal-spaced borders, one bs_freq_res for all envelopes. */
        WB(SBR_FRAME_CLASS_FIXFIX, 2);
        WB(fd->numEnvelopes > 1 ? 1 : 0, 2);
        WB(sbr->bs_freq_res, 1);
    }
#undef WB
    return bits;
}

static int write_sbr_dtdf(const SbrFrameData *fd, BitStream *bs, bool write)
{
    int n_q = fd->numEnvelopes > 1 ? 2 : 1;
    int bits = fd->numEnvelopes + n_q;
    if (write) for (int i = 0; i < bits; i++) PutBit(bs, 0, 1);
    return bits;
}

static int write_sbr_invf(const SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs, int ch, bool write)
{
    if (write) for (int nb = 0; nb < sbr->numNoiseBands; nb++) PutBit(bs, fd->ch[ch].invfMode, 2);
    return sbr->numNoiseBands * 2;
}

static int write_sbr_envelope(const SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs, int ch, bool write)
{
    const SBRHuffEntry *table = fd->eff_amp_res ? f_huff_env_3_0dB : f_huff_env_1_5dB;
    int nsyms = fd->eff_amp_res ? F_HUFF_ENV_3_0DB_NSYMS : F_HUFF_ENV_1_5DB_NSYMS;
    int offset = fd->eff_amp_res ? F_HUFF_ENV_3_0DB_OFFSET : F_HUFF_ENV_1_5DB_OFFSET;
    int bits = 0;

    for (int e = 0; e < fd->numEnvelopes; e++) {
        for (int b = 0; b < sbr->numBands; b++) {
            int val = fd->ch[ch].envData[e][b];
            if (b == 0) {
                int first_bits = fd->eff_amp_res ? 6 : 7;
                if (write) PutBit(bs, clamp_int(val, 0, (1 << first_bits) - 1), first_bits);
                bits += first_bits;
            } else {
                bits += put_huff(bs, write, table, nsyms, offset, val);
            }
        }
    }
    return bits;
}

static int write_sbr_noise(const SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs, int ch, bool write)
{
    int n_q = fd->numEnvelopes > 1 ? 2 : 1;
    int bits = 0;
    for (int ne = 0; ne < n_q; ne++) {
        for (int nb = 0; nb < sbr->numNoiseBands; nb++) {
            int val = fd->ch[ch].noiseData[ne][nb];
            if (nb == 0) {
                if (write) PutBit(bs, clamp_int(val, 0, 30), 5);
                bits += 5;
            } else {
                bits += put_huff(bs, write, f_huff_env_3_0dB, F_HUFF_ENV_3_0DB_NSYMS, F_HUFF_ENV_3_0DB_OFFSET, val);
            }
        }
    }
    return bits;
}

/* Delta-code one parametric-stereo parameter set, either across frequency
 * (reference = the previous band, seeded from 0) or across time (reference = the
 * same band in the last frame that carried this parameter). Returns the bit
 * cost; pass write=false to price an encoding without emitting it.
 *
 * cold/noinline/noclone are deliberate. This runs up to six times per frame from
 * a single caller, and left alone the optimizer inlines it, unrolls the band
 * loop and constprops the flag arguments into three specializations -- several
 * kilobytes of code to emit a few dozen bits, next to a 1024-coefficient core
 * frame it does not measurably affect. */
#if defined(__GNUC__)
__attribute__((cold, noinline, noclone))
#endif
static int write_ps_params(BitStream *bs, bool write, const int *cur, const int *prev,
                           bool timeDelta, const SBRHuffEntry *table, int nsyms, int offset)
{
    int bits = 0;
    int ref = 0;

    for (int b = 0; b < SBR_PS_BANDS; b++) {
        if (timeDelta) ref = prev[b];
        bits += put_huff(bs, write, table, nsyms, offset, cur[b] - ref);
        if (!timeDelta) ref = cur[b];
    }
    return bits;
}

/* ps_data() (ISO/IEC 14496-3 8.6.4), wrapped in the SBR extension framing that
 * carries it. Written twice per frame: once with write=false to measure the
 * payload, because bs_extension_size precedes it, then once for real. Sharing
 * one body between the two passes is the point -- the two must agree exactly or
 * the decoder reads past the extension. That is also why it is noinline: left to
 * itself the optimizer unrolls the two passes back into two copies, which is the
 * duplication this function exists to remove.
 *
 * fd is a delayed slot, so the parameters emitted here describe the same frame
 * as the envelopes alongside them. The dt references in sbr are not delayed:
 * they track emission order, which is what the decoder's own references do. */
#if defined(__GNUC__)
__attribute__((cold, noinline, noclone))
#endif
static int write_ps_extension(SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs, int write)
{
    int bits = 0;
    /* Time-delta needs a reference frame *and* is only worth it when it prices
     * cheaper; frequency-delta is always legal, so it is the fallback. */
    bool iid_dt = false, icc_dt = false;

    if (sbr->iid_prev_valid) {
        int df = write_ps_params(NULL, false, fd->iid, sbr->iid_prev, false,
                                 ps_huff_iid_df, PS_HUFF_IID_NSYMS, PS_HUFF_IID_OFFSET);
        int dt = write_ps_params(NULL, false, fd->iid, sbr->iid_prev, true,
                                 ps_huff_iid_dt, PS_HUFF_IID_NSYMS, PS_HUFF_IID_OFFSET);
        iid_dt = (dt < df);
    }
    if (fd->enable_icc && sbr->icc_prev_valid) {
        int df = write_ps_params(NULL, false, fd->icc, sbr->icc_prev, false,
                                 ps_huff_icc_df, PS_HUFF_ICC_NSYMS, PS_HUFF_ICC_OFFSET);
        int dt = write_ps_params(NULL, false, fd->icc, sbr->icc_prev, true,
                                 ps_huff_icc_dt, PS_HUFF_ICC_NSYMS, PS_HUFF_ICC_OFFSET);
        icc_dt = (dt < df);
    }

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
        PS_WB(0, 1);                    /* enable_ext = 0 */
        PS_WB(0, 1);                    /* bs_frame_class = 0 (fixed borders) */
        /* num_env_tab[0][1] == 1: one envelope for the whole frame. Index 0
         * would mean *zero* envelopes, i.e. "hold the previous frame's image". */
        PS_WB(1, 2);                    /* bs_num_env_idx */

        PS_WB(iid_dt, 1);               /* bs_iid_dt */
        n += write_ps_params(emit ? bs : NULL, emit, fd->iid, sbr->iid_prev,
                             iid_dt, iid_dt ? ps_huff_iid_dt : ps_huff_iid_df,
                             PS_HUFF_IID_NSYMS, PS_HUFF_IID_OFFSET);

        if (fd->enable_icc) {
            PS_WB(icc_dt, 1);           /* bs_icc_dt */
            n += write_ps_params(emit ? bs : NULL, emit, fd->icc, sbr->icc_prev,
                                 icc_dt, icc_dt ? ps_huff_icc_dt : ps_huff_icc_df,
                                 PS_HUFF_ICC_NSYMS, PS_HUFF_ICC_OFFSET);
        }

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

    /* Only advance the time-delta reference on the real write pass: SbrWrite is
     * replayed against a counting sink during rate control, and letting those
     * dry runs move the reference would decode as garbage. */
    if (write) {
        for (int b = 0; b < SBR_PS_BANDS; b++)
            sbr->iid_prev[b] = fd->iid[b];
        sbr->iid_prev_valid = 1;

        if (fd->enable_icc) {
            for (int b = 0; b < SBR_PS_BANDS; b++)
                sbr->icc_prev[b] = fd->icc[b];
            sbr->icc_prev_valid = 1;
        } else {
            /* A frame that carries no ICC does not leave the decoder's
             * reference alone -- it zeroes it (ff_ps_read_data does
             * "memset(ps->icc_par, 0, ...)" on the !enable_icc path). Coding the
             * next ICC frame against our retained copy would then be decoded
             * against zero, and since the bounds check is unsigned
             * ("icc_par[e][b] > 7U") a negative result is not clamped but
             * rejected outright: "illegal icc", and the frame is dropped.
             * Dropping the reference here forces the next ICC frame back to
             * frequency-delta, which is self-contained. */
            sbr->icc_prev_valid = 0;
        }
    }
    return bits;
}

static int write_sbr_data(SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs, int id_aac, bool write)
{
    int bits = 0;
#define WB(v,n) do { if (write) PutBit(bs,(v),(n)); bits += (n); } while(0)
    if (id_aac == ID_CPE) {
        WB(0, 1); WB(0, 1);     /* bs_coupling=0, reserved */
        bits += write_sbr_grid(sbr, fd, bs, write);
        bits += write_sbr_grid(sbr, fd, bs, write);
        bits += write_sbr_dtdf(fd, bs, write);
        bits += write_sbr_dtdf(fd, bs, write);
        bits += write_sbr_invf(sbr, fd, bs, 0, write);
        bits += write_sbr_invf(sbr, fd, bs, 1, write);
        bits += write_sbr_envelope(sbr, fd, bs, 0, write);
        bits += write_sbr_envelope(sbr, fd, bs, 1, write);
        bits += write_sbr_noise(sbr, fd, bs, 0, write);
        bits += write_sbr_noise(sbr, fd, bs, 1, write);
        WB(0, 1); WB(0, 1); WB(0, 1); /* add_harmonic / extended data flags */
    } else {
        WB(0, 1);               /* reserved */
        bits += write_sbr_grid(sbr, fd, bs, write);
        bits += write_sbr_dtdf(fd, bs, write);
        bits += write_sbr_invf(sbr, fd, bs, 0, write);
        bits += write_sbr_envelope(sbr, fd, bs, 0, write);
        bits += write_sbr_noise(sbr, fd, bs, 0, write);
        WB(0, 1);               /* bs_add_harmonic_flag = 0 */
        if (sbr->is_he_v2) {
            WB(1, 1);           /* bs_extended_data = 1 */
            bits += write_ps_extension(sbr, fd, bs, write);
        } else {
            WB(0, 1);           /* bs_extended_data = 0 */
        }
    }
#undef WB
    return bits;
}

/* Emit the full extension_payload body for EXT_SBR_DATA: the 4-bit extension
 * type, the 1-bit header flag, the optional header, and the channel data. */
static int emit_sbr_payload(SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs, int id_aac, int sendHeader, bool write)
{
    int bits = 0;
#define WB(v,n) do { if (write) PutBit(bs,(v),(n)); bits += (n); } while(0)
    WB(SBR_EXT_TYPE_SBR, 4);
    WB(sendHeader, 1);
#undef WB
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

    int totalBits = 0;
#define WB(v,n) do { if (writeFlag) PutBit(bs,(v),(n)); totalBits += (n); } while(0)
    /* fill_element(): id, then 4-bit count with optional 8-bit escape. The
     * decoder reconstructs cnt = 15 + esc_count - 1, hence esc_count = N - 14. */
    WB(ID_FIL, 3);
    if (fillBytes < 15) WB(fillBytes, 4);
    else { WB(15, 4); WB(fillBytes - 14, 8); }
#undef WB

    if (writeFlag) emit_sbr_payload(sbr, fd, bs, id_aac, sendHeader, true);
    totalBits += payloadBits;
    if (padBits > 0) { if (writeFlag) PutBit(bs, 0, padBits); totalBits += padBits; }

    if (writeFlag) { sbr->headerSent = 1; sbr->frameCount++; }
    return totalBits;
}

int SbrContextGetBits(SBRContext *sCtx, BitStream *bs, int channels, int aacObjectType, int writeFlag)
{
    if (IsHEAAC(aacObjectType) && sCtx) {
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
