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

static int write_sbr_header(SBRInfo *sbr, BitStream *bs, bool write)
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

static int write_sbr_grid(SBRInfo *sbr, BitStream *bs, bool write)
{
    int bits = 0;
#define WB(v,n) do { if (write) PutBit(bs,(v),(n)); bits += (n); } while(0)
    if (sbr->frameClass == SBR_FRAME_CLASS_VARFIX) {
        /* VARFIX: variable leading borders, fixed trailing border. Mirrors the
         * inverse of FFmpeg read_sbr_grid()'s VARFIX case: t_env[0]=bs_var_bord_0,
         * each lead border adds 2*bs_rel+2, the trailing border is numTimeSlots
         * (not transmitted), then bs_pointer and per-envelope bs_freq_res. */
        int num_env = sbr->numEnvelopes;
        WB(SBR_FRAME_CLASS_VARFIX, 2);
        WB(sbr->tEnv[0], 2);                 /* bs_var_bord_0 */
        WB(num_env - 1, 2);                  /* bs_num_rel_0   */
        for (int i = 0; i < num_env - 1; i++)
            WB((sbr->tEnv[i + 1] - sbr->tEnv[i] - 2) / 2, 2); /* bs_rel_bord */
        WB(sbr->bsPointer, sbr_ceil_log2[num_env]);
        for (int i = 0; i < num_env; i++)    /* bs_freq_res[1..num_env] */
            WB(sbr->bs_freq_res, 1);
    } else {
        /* FIXFIX: equal-spaced borders, one bs_freq_res for all envelopes. */
        WB(SBR_FRAME_CLASS_FIXFIX, 2);
        WB(sbr->numEnvelopes > 1 ? 1 : 0, 2);
        WB(sbr->bs_freq_res, 1);
    }
#undef WB
    return bits;
}

static int write_sbr_dtdf(SBRInfo *sbr, BitStream *bs, bool write)
{
    int n_q = sbr->numEnvelopes > 1 ? 2 : 1;
    int bits = sbr->numEnvelopes + n_q;
    if (write) for (int i = 0; i < bits; i++) PutBit(bs, 0, 1);
    return bits;
}

static int write_sbr_invf(SBRInfo *sbr, BitStream *bs, int ch, bool write)
{
    if (write) for (int nb = 0; nb < sbr->numNoiseBands; nb++) PutBit(bs, sbr->ch[ch].invfMode, 2);
    return sbr->numNoiseBands * 2;
}

static int write_sbr_envelope(SBRInfo *sbr, BitStream *bs, int ch, bool write)
{
    const SBRHuffEntry *table = sbr->eff_amp_res ? f_huff_env_3_0dB : f_huff_env_1_5dB;
    int nsyms = sbr->eff_amp_res ? F_HUFF_ENV_3_0DB_NSYMS : F_HUFF_ENV_1_5DB_NSYMS;
    int offset = sbr->eff_amp_res ? F_HUFF_ENV_3_0DB_OFFSET : F_HUFF_ENV_1_5DB_OFFSET;
    int bits = 0;

    for (int e = 0; e < sbr->numEnvelopes; e++) {
        for (int b = 0; b < sbr->numBands; b++) {
            int val = sbr->ch[ch].envData[e][b];
            if (b == 0) {
                int first_bits = sbr->eff_amp_res ? 6 : 7;
                if (write) PutBit(bs, clamp_int(val, 0, (1 << first_bits) - 1), first_bits);
                bits += first_bits;
            } else {
                bits += put_huff(bs, write, table, nsyms, offset, val);
            }
        }
    }
    return bits;
}

static int write_sbr_noise(SBRInfo *sbr, BitStream *bs, int ch, bool write)
{
    int n_q = sbr->numEnvelopes > 1 ? 2 : 1;
    int bits = 0;
    for (int ne = 0; ne < n_q; ne++) {
        for (int nb = 0; nb < sbr->numNoiseBands; nb++) {
            int val = sbr->ch[ch].noiseData[ne][nb];
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

static int write_sbr_data(SBRInfo *sbr, BitStream *bs, int id_aac, int write, int aacObjectType)
{
    int bits = 0;
#define WB(v,n) do { if (write) PutBit(bs,(v),(n)); bits += (n); } while(0)
    if (id_aac == ID_CPE) {
        WB(0, 1); WB(0, 1);     /* bs_coupling=0, reserved */
        bits += write_sbr_grid(sbr, bs, write);
        bits += write_sbr_grid(sbr, bs, write);
        bits += write_sbr_dtdf(sbr, bs, write);
        bits += write_sbr_dtdf(sbr, bs, write);
        bits += write_sbr_invf(sbr, bs, 0, write);
        bits += write_sbr_invf(sbr, bs, 1, write);
        bits += write_sbr_envelope(sbr, bs, 0, write);
        bits += write_sbr_envelope(sbr, bs, 1, write);
        bits += write_sbr_noise(sbr, bs, 0, write);
        bits += write_sbr_noise(sbr, bs, 1, write);
        WB(0, 1); WB(0, 1); WB(0, 1); /* add_harmonic / extended data flags */
    } else {
        WB(0, 1);               /* reserved */
        bits += write_sbr_grid(sbr, bs, write);
        bits += write_sbr_dtdf(sbr, bs, write);
        bits += write_sbr_invf(sbr, bs, 0, write);
        bits += write_sbr_envelope(sbr, bs, 0, write);
        bits += write_sbr_noise(sbr, bs, 0, write);
        WB(0, 1);               /* bs_add_harmonic_flag = 0 */
        if (aacObjectType == HE_V2) {
            WB(1, 1);           /* bs_extended_data = 1 */

            static const SBRHuffEntry ps_huff_iid_coarse[29] = {
                { 131067, 17 }, /* -14: binary 11111111111111011 */
                { 131068, 17 }, /* -13: binary 11111111111111100 */
                { 131069, 17 }, /* -12: binary 11111111111111101 */
                { 131066, 17 }, /* -11: binary 11111111111111010 */
                { 65532, 16 },  /* -10: binary 1111111111111100 */
                { 32764, 15 },  /* -9: binary 111111111111100 */
                { 8189, 13 },   /* -8: binary 1111111111101 */
                { 1022, 10 },   /* -7: binary 1111111110 */
                { 510, 9 },     /* -6: binary 111111110 */
                { 126, 7 },     /* -5: binary 1111110 */
                { 60, 6 },      /* -4: binary 111100 */
                { 29, 5 },      /* -3: binary 11101 */
                { 13, 4 },      /* -2: binary 1101 */
                { 5, 3 },       /* -1: binary 101 */
                { 0, 1 },       /*  0: binary 0 */
                { 4, 3 },       /*  1: binary 100 */
                { 12, 4 },      /*  2: binary 1100 */
                { 28, 5 },      /*  3: binary 11100 */
                { 61, 6 },      /*  4: binary 111101 */
                { 62, 6 },      /*  5: binary 111110 */
                { 254, 8 },     /*  6: binary 11111110 */
                { 2046, 11 },   /*  7: binary 11111111110 */
                { 8188, 13 },   /*  8: binary 1111111111100 */
                { 16380, 14 },  /*  9: binary 11111111111100 */
                { 16381, 14 },  /* 10: binary 11111111111101 */
                { 32765, 15 },  /* 11: binary 111111111111101 */
                { 131070, 17 }, /* 12: binary 11111111111111110 */
                { 262142, 18 }, /* 13: binary 111111111111111110 */
                { 262143, 18 }  /* 14: binary 111111111111111111 */
            };

            static const SBRHuffEntry ps_huff_icc_coarse[15] = {
                { 16383, 14 },  /* -7: binary 11111111111111 */
                { 16382, 14 },  /* -6: binary 11111111111110 */
                { 4094, 12 },   /* -5: binary 111111111110 */
                { 1022, 10 },   /* -4: binary 1111111110 */
                { 126, 7 },     /* -3: binary 1111110 */
                { 30, 5 },      /* -2: binary 11110 */
                { 6, 3 },       /* -1: binary 110 */
                { 0, 1 },       /*  0: binary 0 */
                { 2, 2 },       /*  1: binary 10 */
                { 14, 4 },      /*  2: binary 1110 */
                { 62, 6 },      /*  3: binary 111110 */
                { 254, 8 },     /*  4: binary 11111110 */
                { 510, 9 },     /*  5: binary 111111110 */
                { 2046, 11 },   /*  6: binary 11111111110 */
                { 8190, 13 }    /*  7: binary 1111111111110 */
            };

            /* Helper function closure for PS payload counting */
            #define PS_WB(v,n) do { ps_bits += (n); } while(0)
            int ps_bits = 0;
            PS_WB(2, 2);           /* bs_extension_id = EXTENSION_ID_PS = 2 */
            PS_WB(1, 1);           /* enable_ps_header = 1 */
            PS_WB(1, 1);           /* enable_iid = 1 */
            PS_WB(0, 3);           /* iid_mode = 0 (10-band coarse) */
            PS_WB(sbr->enable_icc, 1); /* enable_icc */
            if (sbr->enable_icc) {
                PS_WB(0, 3);       /* icc_mode = 0 (10-band coarse) */
            }
            PS_WB(0, 1);           /* enable_ext = 0 */
            PS_WB(0, 1);           /* frame_class = 0 (FIXFIX) */
            PS_WB(1, 2);           /* num_env = 1 (binary 01) */
            PS_WB(0, 1);           /* dt = 0 (frequency delta-coding) */

            /* Absolute coding for first band of IID */
            int idx = sbr->iid_indices[0];
            int array_idx = clamp_int(idx + 14, 0, 28);
            PS_WB(ps_huff_iid_coarse[array_idx].code, ps_huff_iid_coarse[array_idx].len);

            /* Delta coding for remaining 9 bands of IID */
            for (int b = 1; b < 10; b++) {
                int diff = sbr->iid_indices[b] - sbr->iid_indices[b - 1];
                int diff_array_idx = clamp_int(diff + 14, 0, 28);
                PS_WB(ps_huff_iid_coarse[diff_array_idx].code, ps_huff_iid_coarse[diff_array_idx].len);
            }

            if (sbr->enable_icc) {
                /* Absolute coding for first band of ICC */
                int icc_idx = sbr->icc_indices[0];
                int icc_array_idx = clamp_int(icc_idx + 7, 0, 14);
                PS_WB(0, 1);           /* dt = 0 for ICC */
                PS_WB(ps_huff_icc_coarse[icc_array_idx].code, ps_huff_icc_coarse[icc_array_idx].len);

                /* Delta coding for remaining 9 bands of ICC */
                for (int b = 1; b < 10; b++) {
                    int diff = sbr->icc_indices[b] - sbr->icc_indices[b - 1];
                    int diff_array_idx = clamp_int(diff + 7, 0, 14);
                    PS_WB(ps_huff_icc_coarse[diff_array_idx].code, ps_huff_icc_coarse[diff_array_idx].len);
                }
            }

            /* Padding to byte align SBR extension payload */
            int ps_pad = (8 - (ps_bits & 7)) & 7;
            if (ps_pad > 0) {
                PS_WB(0, ps_pad);
            }
            #undef PS_WB

            int ps_bytes = (ps_bits + 7) / 8;
            if (ps_bytes < 15) {
                WB(ps_bytes, 4);
            } else {
                WB(15, 4);
                WB(ps_bytes - 15, 8);
            }

            /* Perform actual write of the measured, padded PS payload */
            if (write) {
                PutBit(bs, 2, 2);           /* bs_extension_id = EXTENSION_ID_PS = 2 */
                PutBit(bs, 1, 1);           /* enable_ps_header = 1 */
                PutBit(bs, 1, 1);           /* enable_iid = 1 */
                PutBit(bs, 0, 3);           /* iid_mode = 0 (10-band coarse) */
                PutBit(bs, sbr->enable_icc, 1); /* enable_icc */
                if (sbr->enable_icc) {
                    PutBit(bs, 0, 3);       /* icc_mode = 0 (10-band coarse) */
                }
                PutBit(bs, 0, 1);           /* enable_ext = 0 */
                PutBit(bs, 0, 1);           /* frame_class = 0 (FIXFIX) */
                PutBit(bs, 1, 2);           /* num_env = 1 (binary 01) */
                PutBit(bs, 0, 1);           /* dt = 0 */

                int idx2 = sbr->iid_indices[0];
                int array_idx2 = clamp_int(idx2 + 14, 0, 28);
                PutBit(bs, ps_huff_iid_coarse[array_idx2].code, ps_huff_iid_coarse[array_idx2].len);

                for (int b = 1; b < 10; b++) {
                    int diff = sbr->iid_indices[b] - sbr->iid_indices[b - 1];
                    int diff_array_idx = clamp_int(diff + 14, 0, 28);
                    PutBit(bs, ps_huff_iid_coarse[diff_array_idx].code, ps_huff_iid_coarse[diff_array_idx].len);
                }

                if (sbr->enable_icc) {
                    int icc_idx2 = sbr->icc_indices[0];
                    int icc_array_idx2 = clamp_int(icc_idx2 + 7, 0, 14);
                    PutBit(bs, 0, 1);           /* dt = 0 for ICC */
                    PutBit(bs, ps_huff_icc_coarse[icc_array_idx2].code, ps_huff_icc_coarse[icc_array_idx2].len);

                    for (int b = 1; b < 10; b++) {
                        int diff = sbr->icc_indices[b] - sbr->icc_indices[b - 1];
                        int diff_array_idx = clamp_int(diff + 7, 0, 14);
                        PutBit(bs, ps_huff_icc_coarse[diff_array_idx].code, ps_huff_icc_coarse[diff_array_idx].len);
                    }
                }

                if (ps_pad > 0) {
                    PutBit(bs, 0, ps_pad);
                }
            }
            bits += ps_bits;
        } else {
            WB(0, 1);               /* bs_extended_data = 0 */
        }
    }
#undef WB
    return bits;
}

/* Emit the full extension_payload body for EXT_SBR_DATA: the 4-bit extension
 * type, the 1-bit header flag, the optional header, and the channel data. */
static int emit_sbr_payload(SBRInfo *sbr, BitStream *bs, int id_aac, int sendHeader, bool write, int aacObjectType)
{
    int bits = 0;
#define WB(v,n) do { if (write) PutBit(bs,(v),(n)); bits += (n); } while(0)
    WB(SBR_EXT_TYPE_SBR, 4);
    WB(sendHeader, 1);
#undef WB
    if (sendHeader) bits += write_sbr_header(sbr, bs, write);
    bits += write_sbr_data(sbr, bs, id_aac, write, aacObjectType);
    return bits;
}

int SbrWrite(SBRInfo *sbr, BitStream *bs, int id_aac, int writeFlag, int aacObjectType)
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
    int payloadBits = emit_sbr_payload(sbr, NULL, id_aac, sendHeader, false, aacObjectType);
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

    if (writeFlag) emit_sbr_payload(sbr, bs, id_aac, sendHeader, true, aacObjectType);
    totalBits += payloadBits;
    if (padBits > 0) { if (writeFlag) PutBit(bs, 0, padBits); totalBits += padBits; }

    if (writeFlag) { sbr->headerSent = 1; sbr->frameCount++; }
    return totalBits;
}

int SbrContextGetBits(SBRContext *sCtx, BitStream *bs, int channels, int aacObjectType, int writeFlag)
{
    if ((aacObjectType == HE_V1 || aacObjectType == HE_V2) && sCtx) {
        if (sCtx->sbrInfo) {
            int id_aac = (channels > 1) ? ID_CPE : ID_SCE;
            return SbrWrite(sCtx->sbrInfo, bs, id_aac, writeFlag, aacObjectType);
        }
    }
    return 0;
}
