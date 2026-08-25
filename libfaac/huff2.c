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

/* Bitwise branchless non-zero check: returns 1 if x != 0, else 0. */
static inline int is_nonzero(int x)
{
    return (int)(((unsigned int)x | (unsigned int)-x) >> 31);
}

/* Both books of a pair share the index expression and the sign-bit count; only
 * the table differs. One walk, two lookups.
 *
 * bnum is always a pair base -- huffbook takes HCB_ESC without sizing it -- so
 * there is deliberately no escape case. */
static void huffcode_size_pair(int *qs, int len, int bnum, int *bits_a, int *bits_b)
{
    hcode16_t *booka = hmap[bnum];
    hcode16_t *bookb = hmap[bnum + 1];
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
            int idx = DIM_M4*DIM_M4*DIM_M4 * abs(qs[i]) + DIM_M4*DIM_M4 * abs(qs[i+1]) + DIM_M4 * abs(qs[i+2]) + abs(qs[i+3]);
            /* Branchless sign-bit counting using bitwise logic */
            int sign = is_nonzero(qs[i]) + is_nonzero(qs[i+1]) + is_nonzero(qs[i+2]) + is_nonzero(qs[i+3]);
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
            int idx = DIM_M2_7 * abs(qs[i]) + abs(qs[i+1]);
            int sign = is_nonzero(qs[i]) + is_nonzero(qs[i+1]);
            a += booka[idx].len + sign;
            b += bookb[idx].len + sign;
        }
        break;
    case HCB_9:
        for (i = 0; i < len; i += 2) {
            int idx = DIM_M2_12 * abs(qs[i]) + abs(qs[i+1]);
            int sign = is_nonzero(qs[i]) + is_nonzero(qs[i+1]);
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

/* Bitstream mutation function, called once per finalized frame. */
static void huffcode_write(int *qs, int len, int bnum, CoderInfo *coder)
{
    hcode16_t *book = hmap[bnum];
    int i, j;
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
            int idx = DIM_M4*DIM_M4*DIM_M4 * abs(qs[i]) + DIM_M4*DIM_M4 * abs(qs[i+1]) + DIM_M4 * abs(qs[i+2]) + abs(qs[i+3]);
            int blen = book[idx].len;
            int data = book[idx].data;
            for (j = 0; j < 4; j++) {
                if (qs[i+j]) {
                    blen++;
                    data = (data << 1) | (qs[i+j] < 0);
                }
            }
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
            int idx = DIM_M2_7 * abs(qs[i]) + abs(qs[i+1]);
            int blen = book[idx].len;
            int data = book[idx].data;
            for (j = 0; j < 2; j++) {
                if (qs[i+j]) {
                    blen++;
                    data = (data << 1) | (qs[i+j] < 0);
                }
            }
            coder->s[datacnt].data = data;
            coder->s[datacnt++].len = blen;
        }
        break;
    case HCB_9:
    case HCB_10:
        for (i = 0; i < len; i += 2) {
            int idx = DIM_M2_12 * abs(qs[i]) + abs(qs[i+1]);
            int blen = book[idx].len;
            int data = book[idx].data;
            for (j = 0; j < 2; j++) {
                if (qs[i+j]) {
                    blen++;
                    data = (data << 1) | (qs[i+j] < 0);
                }
            }
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

/* Sizing function for single book without writing bitstream entries */
static int huffcode_size_single(const int *qs, int len, int bnum)
{
    hcode16_t *book = hmap[bnum];
    int bits = 0;
    int i;

    switch (bnum) {
    case HCB_1:
    case HCB_2:
        for (i = 0; i < len; i += 4) {
            int idx = 40 + DIM_S4*DIM_S4*DIM_S4 * qs[i] + DIM_S4*DIM_S4 * qs[i+1] + DIM_S4 * qs[i+2] + qs[i+3];
            bits += book[idx].len;
        }
        break;
    case HCB_3:
    case HCB_4:
        for (i = 0; i < len; i += 4) {
            int idx = DIM_M4*DIM_M4*DIM_M4 * abs(qs[i]) + DIM_M4*DIM_M4 * abs(qs[i+1]) + DIM_M4 * abs(qs[i+2]) + abs(qs[i+3]);
            int sign = is_nonzero(qs[i]) + is_nonzero(qs[i+1]) + is_nonzero(qs[i+2]) + is_nonzero(qs[i+3]);
            bits += book[idx].len + sign;
        }
        break;
    case HCB_5:
    case HCB_6:
        for (i = 0; i < len; i += 2) {
            int idx = 40 + DIM_S2 * qs[i] + qs[i+1];
            bits += book[idx].len;
        }
        break;
    case HCB_7:
    case HCB_8:
        for (i = 0; i < len; i += 2) {
            int idx = DIM_M2_7 * abs(qs[i]) + abs(qs[i+1]);
            int sign = is_nonzero(qs[i]) + is_nonzero(qs[i+1]);
            bits += book[idx].len + sign;
        }
        break;
    case HCB_9:
    case HCB_10:
        for (i = 0; i < len; i += 2) {
            int idx = DIM_M2_12 * abs(qs[i]) + abs(qs[i+1]);
            int sign = is_nonzero(qs[i]) + is_nonzero(qs[i+1]);
            bits += book[idx].len + sign;
        }
        break;
    case HCB_ESC:
        for (i = 0; i < len; i += 2) {
            int x0 = abs(qs[i]), x1 = abs(qs[i+1]);
            int v0 = (x0 > LAV_ESC) ? LAV_ESC : x0;
            int v1 = (x1 > LAV_ESC) ? LAV_ESC : x1;
            int idx = DIM_ESC * v0 + v1;
            int sign = is_nonzero(qs[i]) + is_nonzero(qs[i+1]);
            bits += book[idx].len + sign;
            if (x0 >= LAV_ESC) {
                bits += escape(x0, NULL);
            }
            if (x1 >= LAV_ESC) {
                bits += escape(x1, NULL);
            }
        }
        break;
    default:
        break;
    }
    return bits;
}

#define DP_INF 100000000
#define NUM_SECTION_BOOKS 16

/* Pick the codebook that minimizes the bit cost for a given band. */
int huffbook(CoderInfo *coder, int *qs, int len)
{
    int i, maxq = 0;
    int bookmin = HCB_ZERO;
    int band = coder->bandcnt;

    for (i = 0; i < len; i++) {
        int q = abs(qs[i]);
        if (maxq < q) maxq = q;
    }

    coder->maxq[band] = maxq;

    for (int cb = 0; cb < NUM_SECTION_BOOKS; cb++) {
        coder->bit_cost[band][cb] = DP_INF;
    }

    if (maxq == 0) {
        coder->bit_cost[band][HCB_ZERO] = 0;
        bookmin = HCB_ZERO;
    } else {
        int pair_base;
        if (maxq <= LAV_1) pair_base = HCB_1;
        else if (maxq <= LAV_2) pair_base = HCB_3;
        else if (maxq <= LAV_4) pair_base = HCB_5;
        else if (maxq <= LAV_7) pair_base = HCB_7;
        else if (maxq <= LAV_12) pair_base = HCB_9;
        else pair_base = HCB_ESC;

        if (pair_base != HCB_ESC) {
            int len1, len2;
            huffcode_size_pair(qs, len, pair_base, &len1, &len2);
            coder->bit_cost[band][pair_base] = len1;
            coder->bit_cost[band][pair_base + 1] = len2;
            bookmin = (len2 < len1) ? pair_base + 1 : pair_base;
        } else {
            coder->bit_cost[band][HCB_ESC] = huffcode_size_single(qs, len, HCB_ESC);
            bookmin = HCB_ESC;
        }

        /* Evaluate higher LAV codebooks as alternative valid assignments */
        if (maxq <= LAV_1) {
            int len3, len4, len5, len6, len7, len8, len9, len10;
            huffcode_size_pair(qs, len, HCB_3, &len3, &len4);
            coder->bit_cost[band][HCB_3] = len3;
            coder->bit_cost[band][HCB_4] = len4;
            huffcode_size_pair(qs, len, HCB_5, &len5, &len6);
            coder->bit_cost[band][HCB_5] = len5;
            coder->bit_cost[band][HCB_6] = len6;
            huffcode_size_pair(qs, len, HCB_7, &len7, &len8);
            coder->bit_cost[band][HCB_7] = len7;
            coder->bit_cost[band][HCB_8] = len8;
            huffcode_size_pair(qs, len, HCB_9, &len9, &len10);
            coder->bit_cost[band][HCB_9] = len9;
            coder->bit_cost[band][HCB_10] = len10;
            coder->bit_cost[band][HCB_ESC] = huffcode_size_single(qs, len, HCB_ESC);
        } else if (maxq <= LAV_2) {
            int len5, len6, len7, len8, len9, len10;
            huffcode_size_pair(qs, len, HCB_5, &len5, &len6);
            coder->bit_cost[band][HCB_5] = len5;
            coder->bit_cost[band][HCB_6] = len6;
            huffcode_size_pair(qs, len, HCB_7, &len7, &len8);
            coder->bit_cost[band][HCB_7] = len7;
            coder->bit_cost[band][HCB_8] = len8;
            huffcode_size_pair(qs, len, HCB_9, &len9, &len10);
            coder->bit_cost[band][HCB_9] = len9;
            coder->bit_cost[band][HCB_10] = len10;
            coder->bit_cost[band][HCB_ESC] = huffcode_size_single(qs, len, HCB_ESC);
        } else if (maxq <= LAV_4) {
            int len7, len8, len9, len10;
            huffcode_size_pair(qs, len, HCB_7, &len7, &len8);
            coder->bit_cost[band][HCB_7] = len7;
            coder->bit_cost[band][HCB_8] = len8;
            huffcode_size_pair(qs, len, HCB_9, &len9, &len10);
            coder->bit_cost[band][HCB_9] = len9;
            coder->bit_cost[band][HCB_10] = len10;
            coder->bit_cost[band][HCB_ESC] = huffcode_size_single(qs, len, HCB_ESC);
        } else if (maxq <= LAV_7) {
            int len9, len10;
            huffcode_size_pair(qs, len, HCB_9, &len9, &len10);
            coder->bit_cost[band][HCB_9] = len9;
            coder->bit_cost[band][HCB_10] = len10;
            coder->bit_cost[band][HCB_ESC] = huffcode_size_single(qs, len, HCB_ESC);
        } else if (maxq <= LAV_12) {
            coder->bit_cost[band][HCB_ESC] = huffcode_size_single(qs, len, HCB_ESC);
        }
    }

    coder->book[coder->bandcnt] = bookmin;
    return 0;
}

void huffcode_write_band(CoderInfo *coder, int *qs, int len, int bnum)
{
    huffcode_write(qs, len, bnum, coder);
}

/* Optimization of sectioning using Viterbi Dynamic Programming trellis search */
void optimize_section_codebooks(CoderInfo *coder, int gnum)
{
    (void)gnum;
    int sfbn = coder->sfbn;
    int max_run = (coder->block_type == ONLY_SHORT_WINDOW) ? 7 : 31;
    int run_bits = (coder->block_type == ONLY_SHORT_WINDOW) ? 3 : 5;
    int section_header_cost = 4 + run_bits; /* 4-bit book index + 3/5-bit run length */

    int start_band = coder->bandcnt - sfbn;
    int sb;

    /* DP matrices: dp[sb][cb] = min bits up to band sb ending in cb, run[sb][cb] = run length of cb */
    int dp[NSFB_LONG][NUM_SECTION_BOOKS];
    int run[NSFB_LONG][NUM_SECTION_BOOKS];
    int back_cb[NSFB_LONG][NUM_SECTION_BOOKS];

    for (int cb = 0; cb < NUM_SECTION_BOOKS; cb++) {
        int band_idx = start_band + 0;
        if (coder->bit_cost[band_idx][cb] < DP_INF) {
            dp[0][cb] = section_header_cost + coder->bit_cost[band_idx][cb];
            run[0][cb] = 1;
            back_cb[0][cb] = cb;
        } else {
            dp[0][cb] = DP_INF;
        }
    }

    for (sb = 1; sb < sfbn; sb++) {
        int band_idx = start_band + sb;

        for (int cb = 0; cb < NUM_SECTION_BOOKS; cb++) {
            dp[sb][cb] = DP_INF;
            run[sb][cb] = 0;
            back_cb[sb][cb] = -1;

            if (coder->bit_cost[band_idx][cb] >= DP_INF) continue;

            /* Option 1: Continue same codebook cb from sb-1 */
            if (dp[sb - 1][cb] < DP_INF) {
                int prev_run = run[sb - 1][cb];
                int extra_hdr = (prev_run % max_run == 0) ? run_bits : 0;
                int cost = dp[sb - 1][cb] + extra_hdr + coder->bit_cost[band_idx][cb];
                dp[sb][cb] = cost;
                run[sb][cb] = prev_run + 1;
                back_cb[sb][cb] = cb;
            }

            /* Option 2: Switch codebook from any prev_cb at sb-1 */
            for (int prev_cb = 0; prev_cb < NUM_SECTION_BOOKS; prev_cb++) {
                if (prev_cb == cb) continue;
                if (dp[sb - 1][prev_cb] < DP_INF) {
                    int cost = dp[sb - 1][prev_cb] + section_header_cost + coder->bit_cost[band_idx][cb];
                    if (cost < dp[sb][cb]) {
                        dp[sb][cb] = cost;
                        run[sb][cb] = 1;
                        back_cb[sb][cb] = prev_cb;
                    }
                }
            }
        }
    }

    /* Find best ending codebook at sfbn - 1 */
    int best_cb = -1;
    int min_total_bits = DP_INF;
    for (int cb = 0; cb < NUM_SECTION_BOOKS; cb++) {
        if (dp[sfbn - 1][cb] < min_total_bits) {
            min_total_bits = dp[sfbn - 1][cb];
            best_cb = cb;
        }
    }

    if (best_cb != -1) {
        /* Traceback and update coder->book[] */
        int curr_cb = best_cb;
        int opt_books[NSFB_LONG];
        for (int sb_idx = sfbn - 1; sb_idx >= 0; sb_idx--) {
            opt_books[sb_idx] = curr_cb;
            curr_cb = back_cb[sb_idx][curr_cb];
        }

        /* Update coder->book[] */
        for (sb = 0; sb < sfbn; sb++) {
            int band_idx = start_band + sb;
            coder->book[band_idx] = opt_books[sb];
        }
    }
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
