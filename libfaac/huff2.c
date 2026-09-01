/*
 * FAAC - Freeware Advanced Audio Coder
 * Huffman coding per ISO/IEC 14496-3
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

#include <stdio.h>
#include <stdlib.h>
#include "coder.h"
#include "huffdata.h"
#include "huff2.h"
#include "bitstream.h"
#include "util.h"

/* Escape coding for HCB_ESC as per ISO/IEC 14496-3.
 * Represents values |q| >= 16 by sending 16 plus an escape suffix. */
static int escape(int x, int *code)
{
    if (x > MAX_HUFF_ESC_VAL) {
        fprintf(stderr, "Huffman escape value out of range: %d\n", x);
        return 0;
    }

    int preflen = 31 - CountLeadingZeros(x) - 4;
    int base = 1 << (preflen + 4);

    if (code) {
        /* Unary prefix: preflen 1s followed by a 0 */
        *code = (1 << (preflen + 1)) - 2;
        /* Escape suffix is (preflen+4) bits: base starts at 16 (= 2^4), so the
         * value field is always at least 4 bits; each additional doubling adds one. */
        *code = (*code << (preflen + 4)) | (x - base);
    }

    return (preflen + 1) + (preflen + 4);
}

static hcode16_t * const hmap[12] = {
    NULL, book01, book02, book03, book04, book05,
    book06, book07, book08, book09, book10, book11
};


/* Both books of a pair share the index expression and the sign-bit count; only
 * the table differs. One walk, two lookups.
 *
 * ISO 14496-3 multidimensional Huffman section tuple indexing. Constant dimensions
 * (DIM_S4, DIM_M4, DIM_S2, DIM_M2_7, DIM_M2_12) allow constant folding into shift-adds.
 *
 * bnum is always a pair base -- huffbook takes HCB_ESC without sizing it -- so
 * there is deliberately no escape case. */
static void huffcode_size_pair(const int * __restrict qs, int len, int bnum, int *bits_a, int *bits_b)
{
    const hcode16_t *booka = hmap[bnum];
    const hcode16_t *bookb = hmap[bnum + 1];
    int a = 0, b = 0;
    int i;

    switch (bnum) {
    case HCB_1:
        for (i = 0; i < len; i += 4) {
            int idx = 40 + DIM_S4*DIM_S4*DIM_S4 * qs[i] + DIM_S4*DIM_S4 * qs[i+1] + DIM_S4 * qs[i+2] + qs[i+3];
            a += booka[idx].len;
            b += bookb[idx].len;
        }
        break;
    case HCB_3:
        for (i = 0; i < len; i += 4) {
            int a0 = abs(qs[i]), a1 = abs(qs[i+1]), a2 = abs(qs[i+2]), a3 = abs(qs[i+3]);
            int idx = DIM_M4*DIM_M4*DIM_M4 * a0 + DIM_M4*DIM_M4 * a1 + DIM_M4 * a2 + a3;
            int sign = (a0 != 0) + (a1 != 0) + (a2 != 0) + (a3 != 0);
            a += booka[idx].len + sign;
            b += bookb[idx].len + sign;
        }
        break;
    case HCB_5:
        for (i = 0; i < len; i += 2) {
            int idx = 40 + DIM_S2 * qs[i] + qs[i+1];
            a += booka[idx].len;
            b += bookb[idx].len;
        }
        break;
    case HCB_7:
        for (i = 0; i < len; i += 2) {
            int a0 = abs(qs[i]), a1 = abs(qs[i+1]);
            int idx = DIM_M2_7 * a0 + a1;
            int sign = (a0 != 0) + (a1 != 0);
            a += booka[idx].len + sign;
            b += bookb[idx].len + sign;
        }
        break;
    case HCB_9:
        for (i = 0; i < len; i += 2) {
            int a0 = abs(qs[i]), a1 = abs(qs[i+1]);
            int idx = DIM_M2_12 * a0 + a1;
            int sign = (a0 != 0) + (a1 != 0);
            a += booka[idx].len + sign;
            b += bookb[idx].len + sign;
        }
        break;
    default:
        break;
    }

    *bits_a = a;
    *bits_b = b;
}

/* Private O(1) constant lookup tables for Largest Absolute Value (LAV) and pair base book. */
static const uint16_t huff_book_lav[16] = {
    0,                                                          /* HCB_ZERO */
    LAV_1, LAV_1, LAV_2, LAV_2, LAV_4, LAV_4,                   /* HCB_1..6 */
    LAV_7, LAV_7, LAV_12, LAV_12, MAX_HUFF_ESC_VAL,             /* HCB_7..ESC */
    0, 0, 0, 0                                                  /* HCB_DELTA, PNS, IS2, IS */
};

static const uint8_t huff_maxq_base_lut[17] = {
    HCB_ZERO,                                                   /* maxq = 0 */
    HCB_1,                                                      /* maxq = 1 (LAV_1) */
    HCB_3,                                                      /* maxq = 2 (LAV_2) */
    HCB_5, HCB_5,                                                /* maxq = 3,4 (LAV_4) */
    HCB_7, HCB_7, HCB_7,                                        /* maxq = 5,6,7 (LAV_7) */
    HCB_9, HCB_9, HCB_9, HCB_9, HCB_9,                          /* maxq = 8..12 (LAV_12) */
    HCB_ESC, HCB_ESC, HCB_ESC, HCB_ESC                          /* maxq = 13..16 (HCB_ESC) */
};

static inline int huffbook_lav(int book)
{
    return (book >= 0 && book < 16) ? huff_book_lav[book] : 0;
}

static inline int huffbook_maxq_base(int maxq)
{
    return (maxq >= 0 && maxq <= 16) ? huff_maxq_base_lut[maxq] : HCB_ESC;
}

/* Fast pointer-increment single-book sizing. */
static int huffbook_size_single(const int * __restrict qs, int len, int book)
{
    if (book < HCB_1 || book > HCB_ESC) return 0;
    const hcode16_t * __restrict hbook = hmap[book];
    int i, bits = 0;

    switch (book) {
    case HCB_1:
    case HCB_2:
        for (i = 0; i < len; i += 4) {
            int idx = 40 + DIM_S4*DIM_S4*DIM_S4 * qs[i] + DIM_S4*DIM_S4 * qs[i+1] + DIM_S4 * qs[i+2] + qs[i+3];
            bits += hbook[idx].len;
        }
        break;
    case HCB_3:
    case HCB_4:
        for (i = 0; i < len; i += 4) {
            int a0 = abs(qs[i]), a1 = abs(qs[i+1]), a2 = abs(qs[i+2]), a3 = abs(qs[i+3]);
            int idx = DIM_M4*DIM_M4*DIM_M4 * a0 + DIM_M4*DIM_M4 * a1 + DIM_M4 * a2 + a3;
            bits += hbook[idx].len + (a0 != 0) + (a1 != 0) + (a2 != 0) + (a3 != 0);
        }
        break;
    case HCB_5:
    case HCB_6:
        for (i = 0; i < len; i += 2) {
            int idx = 40 + DIM_S2 * qs[i] + qs[i+1];
            bits += hbook[idx].len;
        }
        break;
    case HCB_7:
    case HCB_8:
        for (i = 0; i < len; i += 2) {
            int a0 = abs(qs[i]), a1 = abs(qs[i+1]);
            int idx = DIM_M2_7 * a0 + a1;
            bits += hbook[idx].len + (a0 != 0) + (a1 != 0);
        }
        break;
    case HCB_9:
    case HCB_10:
        for (i = 0; i < len; i += 2) {
            int a0 = abs(qs[i]), a1 = abs(qs[i+1]);
            int idx = DIM_M2_12 * a0 + a1;
            bits += hbook[idx].len + (a0 != 0) + (a1 != 0);
        }
        break;
    case HCB_ESC:
        for (i = 0; i < len; i += 2) {
            int x0 = abs(qs[i]), x1 = abs(qs[i+1]);
            int v0 = (x0 > LAV_ESC) ? LAV_ESC : x0;
            int v1 = (x1 > LAV_ESC) ? LAV_ESC : x1;
            int idx = DIM_ESC * v0 + v1;
            bits += hbook[idx].len + (x0 != 0) + (x1 != 0);
            if (x0 >= LAV_ESC) bits += escape(x0, NULL);
            if (x1 >= LAV_ESC) bits += escape(x1, NULL);
        }
        break;
    default:
        break;
    }
    return bits;
}

/* Bitstream mutation function, called once per finalized frame. */
static void huffcode_write(const int * __restrict qs, int len, int bnum, CoderInfo *coder)
{
    const hcode16_t *book = hmap[bnum];
    int i;
    int datacnt = coder->datacnt;

    switch (bnum) {
    case HCB_1:
    case HCB_2:
        for (i = 0; i < len; i += 4) {
            int idx = 40 + DIM_S4*DIM_S4*DIM_S4 * qs[i] + DIM_S4*DIM_S4 * qs[i+1] + DIM_S4 * qs[i+2] + qs[i+3];
            coder->s[datacnt].data = book[idx].data;
            coder->s[datacnt++].len = book[idx].len;
        }
        break;
    case HCB_3:
    case HCB_4:
        for (i = 0; i < len; i += 4) {
            int q0 = qs[i], q1 = qs[i+1], q2 = qs[i+2], q3 = qs[i+3];
            int a0 = abs(q0), a1 = abs(q1), a2 = abs(q2), a3 = abs(q3);
            int idx = DIM_M4*DIM_M4*DIM_M4 * a0 + DIM_M4*DIM_M4 * a1 + DIM_M4 * a2 + a3;
            int blen = book[idx].len;
            int data = book[idx].data;
            if (q0) { blen++; data = (data << 1) | (q0 < 0); }
            if (q1) { blen++; data = (data << 1) | (q1 < 0); }
            if (q2) { blen++; data = (data << 1) | (q2 < 0); }
            if (q3) { blen++; data = (data << 1) | (q3 < 0); }
            coder->s[datacnt].data = data;
            coder->s[datacnt++].len = blen;
        }
        break;
    case HCB_5:
    case HCB_6:
        for (i = 0; i < len; i += 2) {
            int idx = 40 + DIM_S2 * qs[i] + qs[i+1];
            coder->s[datacnt].data = book[idx].data;
            coder->s[datacnt++].len = book[idx].len;
        }
        break;
    case HCB_7:
    case HCB_8:
        for (i = 0; i < len; i += 2) {
            int q0 = qs[i], q1 = qs[i+1];
            int a0 = abs(q0), a1 = abs(q1);
            int idx = DIM_M2_7 * a0 + a1;
            int blen = book[idx].len;
            int data = book[idx].data;
            if (q0) { blen++; data = (data << 1) | (q0 < 0); }
            if (q1) { blen++; data = (data << 1) | (q1 < 0); }
            coder->s[datacnt].data = data;
            coder->s[datacnt++].len = blen;
        }
        break;
    case HCB_9:
    case HCB_10:
        for (i = 0; i < len; i += 2) {
            int q0 = qs[i], q1 = qs[i+1];
            int a0 = abs(q0), a1 = abs(q1);
            int idx = DIM_M2_12 * a0 + a1;
            int blen = book[idx].len;
            int data = book[idx].data;
            if (q0) { blen++; data = (data << 1) | (q0 < 0); }
            if (q1) { blen++; data = (data << 1) | (q1 < 0); }
            coder->s[datacnt].data = data;
            coder->s[datacnt++].len = blen;
        }
        break;
    case HCB_ESC:
        for (i = 0; i < len; i += 2) {
            int x0 = abs(qs[i]), x1 = abs(qs[i+1]);
            int v0 = (x0 > LAV_ESC) ? LAV_ESC : x0;
            int v1 = (x1 > LAV_ESC) ? LAV_ESC : x1;
            int idx = DIM_ESC * v0 + v1;
            int blen = book[idx].len;
            int data = book[idx].data;
            if (qs[i]) {
                blen++;
                data = (data << 1) | (qs[i] < 0);
            }
            if (qs[i+1]) {
                blen++;
                data = (data << 1) | (qs[i+1] < 0);
            }
            coder->s[datacnt].data = data;
            coder->s[datacnt++].len = blen;
            if (x0 >= LAV_ESC) {
                int esc_code = 0;
                int esc_len = escape(x0, &esc_code);
                coder->s[datacnt].data = esc_code;
                coder->s[datacnt++].len = esc_len;
            }
            if (x1 >= LAV_ESC) {
                int esc_code = 0;
                int esc_len = escape(x1, &esc_code);
                coder->s[datacnt].data = esc_code;
                coder->s[datacnt++].len = esc_len;
            }
        }
        break;
    default:
        break;
    }

    coder->datacnt = datacnt;
}

/* Single-pass codebook assignment with intra-pair zero-scan section merging.
 * Precomputes optimal pair base and bit cost ONCE per band.
 * Reuses already-computed intra-pair lengths (len1/len2) without rescanning qs. */
int huffbook_assign(CoderInfo *coder, const int *qs, int len, int maxq, int prev_book, int section_run)
{
    int bookmin = HCB_ZERO;

    if (maxq > 0) {
        int pair_base = huffbook_maxq_base(maxq);
        int opt_book, bits_opt;
        int len1 = 0, len2 = 0;

        if (pair_base != HCB_ESC) {
            huffcode_size_pair(qs, len, pair_base, &len1, &len2);
            opt_book = (len2 < len1) ? pair_base + 1 : pair_base;
            bits_opt = (len2 < len1) ? len2 : len1;
        } else {
            opt_book = HCB_ESC;
            bits_opt = huffbook_size_single(qs, len, HCB_ESC);
        }

        int max_section_run = (coder->block_type == ONLY_SHORT_WINDOW) ? 7 : 31;
        int prev_base = huffbook_maxq_base(huffbook_lav(prev_book));

        /* Codebook Distance Pruning: restricting cross-pair section merging to
         * prev_base <= pair_base + 1 guarantees that merging only occurs within the
         * SAME range pair (zero extra scans) or immediately adjacent pair, eliminating
         * secondary memory scans on high-LAV spectral bands and matching master speed. */
        if (prev_book >= HCB_1 && prev_book <= HCB_ESC && maxq <= huffbook_lav(prev_book) &&
            section_run < max_section_run && prev_base <= pair_base + 1)
        {
            if (opt_book == prev_book) {
                bookmin = prev_book;
            } else if (pair_base != HCB_ESC && (prev_book == pair_base || prev_book == pair_base + 1)) {
                /* Intra-pair zero-scan optimization: prev_book belongs to pair_base's range pair;
                 * its bit length was ALREADY computed by huffcode_size_pair! Zero extra scans. */
                int bits_prev = (prev_book == pair_base) ? len1 : len2;
                int section_hdr_bits = (coder->block_type == ONLY_SHORT_WINDOW) ? 7 : 9;
                if (bits_prev - bits_opt < section_hdr_bits) {
                    bookmin = prev_book;
                } else {
                    bookmin = opt_book;
                }
            } else {
                int bits_prev = huffbook_size_single(qs, len, prev_book);
                int section_hdr_bits = (coder->block_type == ONLY_SHORT_WINDOW) ? 7 : 9;
                if (bits_prev - bits_opt < section_hdr_bits) {
                    bookmin = prev_book;
                } else {
                    bookmin = opt_book;
                }
            }
        } else {
            bookmin = opt_book;
        }

        huffcode_write(qs, len, bookmin, coder);
    }

    coder->book[coder->bandcnt++] = bookmin;
    return 0;
}

/* Encode the section data (codebook indices and run lengths). */
int writebooks(CoderInfo *coder, BitStream *stream, int write)
{
    int bits = 0;
    /* Section run field is 3 bits for short windows (max 7 windows/section) and
     * 5 bits for long windows (max 31 bands/section) — ISO 14496-3 §4.6.8.2. */
    int max_run = (coder->block_type == ONLY_SHORT_WINDOW) ? 7 : 31;
    int run_bits = (coder->block_type == ONLY_SHORT_WINDOW) ? 3 : 5;
    int g;
    BitAccumulator acc = {0};

    if (write) AccumBegin(&acc, stream);

    for (g = 0; g < coder->groups.n; g++) {
        int b = g * coder->sfbn;
        int end = b + coder->sfbn;
        while (b < end) {
            int book = coder->book[b];
            int run = 0;
            while (b + run < end && coder->book[b + run] == book) run++;
            b += run;

            if (write) AccumPutBits(&acc, (uint32_t)book, 4);
            bits += 4;

            while (run >= max_run) {
                if (write) AccumPutBits(&acc, (uint32_t)max_run, run_bits);
                bits += run_bits;
                run -= max_run;
            }
            if (write) AccumPutBits(&acc, (uint32_t)run, run_bits);
            bits += run_bits;
        }
    }

    if (write) AccumEnd(&acc);
    return bits;
}

/* Encode scalefactor deltas using HCB_DELTA (book12). */
int writesf(CoderInfo *coder, BitStream *stream, int write)
{
    int i, bits = 0;
    int lastsf = coder->global_gain;
    int lastis = 0;
    int lastpns = coder->global_gain - SF_PNS_OFFSET;
    int is_first_pns = 1;
    BitAccumulator acc = {0};

    if (write) AccumBegin(&acc, stream);

    for (i = 0; i < coder->bandcnt; i++) {
        int book = coder->book[i];
        int val = coder->sf[i];
        int diff, code, len;

        if (book == HCB_ZERO || book == HCB_NONE) continue;

        if (book == HCB_INTENSITY || book == HCB_INTENSITY2) {
            diff = clamp_sf_diff(val - lastis);
            lastis += diff;
        } else if (book == HCB_PNS) {
            diff = val - lastpns;
            if (is_first_pns) {
                /* First PNS band is coded as an absolute 9-bit value (biased by 256)
                 * because there is no prior PNS entry to delta from yet. */
                if (write) AccumPutBits(&acc, (uint32_t)(diff + 256), 9);
                bits += 9;
                lastpns = val;
                is_first_pns = 0;
                continue;
            }
            diff = clamp_sf_diff(diff);
            lastpns += diff;
        } else {
            diff = clamp_sf_diff(val - lastsf);
            lastsf += diff;
        }

        code = book12[SF_DELTA + diff].data;
        len = book12[SF_DELTA + diff].len;
        if (write) AccumPutBits(&acc, (uint32_t)code, len);
        bits += len;
    }

    if (write) AccumEnd(&acc);
    return bits;
}
