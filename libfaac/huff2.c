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
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
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

/* Fast bit-length sizing for quantization trials. */
static int huffcode_size(int *qs, int len, int bnum)
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
            bits += book[idx].len;
            /* Branchless sign-bit counting using bitwise logic */
            bits += is_nonzero(qs[i]) + is_nonzero(qs[i+1]) + is_nonzero(qs[i+2]) + is_nonzero(qs[i+3]);
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
            bits += book[idx].len;
            bits += is_nonzero(qs[i]) + is_nonzero(qs[i+1]);
        }
        break;
    case HCB_9:
    case HCB_10:
        for (i = 0; i < len; i += 2) {
            int idx = DIM_M2_12 * abs(qs[i]) + abs(qs[i+1]);
            bits += book[idx].len;
            bits += is_nonzero(qs[i]) + is_nonzero(qs[i+1]);
        }
        break;
    case HCB_ESC:
        for (i = 0; i < len; i += 2) {
            int x0 = abs(qs[i]), x1 = abs(qs[i+1]);
            int v0 = (x0 > LAV_ESC) ? LAV_ESC : x0;
            int v1 = (x1 > LAV_ESC) ? LAV_ESC : x1;
            int idx = DIM_ESC * v0 + v1;
            bits += book[idx].len;
            bits += is_nonzero(qs[i]) + is_nonzero(qs[i+1]);
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

/* Choose each band's codebook, deferring serialization: the coefficients are
   kept so MergeSections can re-cost and merge sections before they're emitted. */
int huffbook(CoderInfo *coder, int *qs, int len)
{
    int i, maxq = 0;
    int bookmin = HCB_ZERO, lenmin = 0;
    int band = coder->bandcnt;

    for (i = 0; i < len; i++) {
        int q = abs(qs[i]);
        if (maxq < q) maxq = q;
    }

    if (maxq > 0) {
        /* Each spectral book covers values up to its LAV; select the range-pair
         * whose lower book just fits maxq, then pick the partner if it costs fewer
         * bits — both books in a pair cover the same amplitude range but use
         * different codeword assignments optimized for different spectral shapes. */
        int pair_base;
        int altlen;
        if (maxq <= LAV_1) pair_base = HCB_1;
        else if (maxq <= LAV_2) pair_base = HCB_3;
        else if (maxq <= LAV_4) pair_base = HCB_5;
        else if (maxq <= LAV_7) pair_base = HCB_7;
        else if (maxq <= LAV_12) pair_base = HCB_9;
        else pair_base = HCB_ESC;

        if (pair_base != HCB_ESC) {
            int len1 = huffcode_size(qs, len, pair_base);
            int len2 = huffcode_size(qs, len, pair_base + 1);
            if (len2 < len1) { bookmin = pair_base + 1; lenmin = len2; altlen = len1; }
            else             { bookmin = pair_base;     lenmin = len1; altlen = len2; }
        } else {
            bookmin = HCB_ESC;
            lenmin = huffcode_size(qs, len, HCB_ESC);
            altlen = lenmin;   /* ESC has no pair partner */
        }

        /* qs already points into coder->quant (quantized in place): no copy. */
        coder->bandmeta[band].off = coder->quantcnt;
        coder->bandmeta[band].maxq = maxq;
        coder->bandmeta[band].bits = lenmin;
        coder->bandmeta[band].altbits = altlen;
        coder->quantcnt += len;
    }

    /* Record the chosen book at the current band slot, but do NOT advance
       bandcnt: the caller (BlocQuant in quantize.c) owns that increment after
       it has also stored the band's scalefactor. */
    coder->book[band] = bookmin;
    return 0;
}

/* Spectral lines in band b (group length x sfb width); bands are group-major
   (b = group*sfbn + sfb) like writebooks. */
static int band_length(const CoderInfo *coder, int b)
{
    int g  = b / coder->sfbn;
    int sb = b - g * coder->sfbn;
    int width = coder->sfb_offset[sb + 1] - coder->sfb_offset[sb];
    return coder->groups.len[g] * width;
}

/* Header bits for one book run: 4-bit id + a run field per max_run chunk plus a
   final one (mirrors writebooks). */
static inline int run_header_bits(int run, int max_run, int run_bits)
{
    return 4 + run_bits * (run / max_run + 1);
}

/* Codeword cost of coded band b under `book`. */
static int band_cost(CoderInfo *coder, int b, int book)
{
    return huffcode_size(coder->quant + coder->bandmeta[b].off, band_length(coder, b), book);
}

/* A mismatched section wider than this can't repay a header saving under a
   higher book, so skip it — also bounds the per-merge re-sizing work. */
#define CROSS_MERGE_MAX_LINES 16

/* Fuse two adjacent coded sections under one range-pair book when the section
   header saved beats the extra codeword bits. The book only widens to a range
   one section already spans, so coefficients and scalefactors are untouched —
   lossless, just less framing. Per-band costs under both pair members are
   precomputed (huffbook: bits/altbits), so only a small lower-range section is
   ever re-sized; same-book neighbours already fuse for free in writebooks. */
void MergeSections(CoderInfo *coder)
{
    int max_run  = (coder->block_type == ONLY_SHORT_WINDOW) ? 7 : 31;
    int run_bits = (coder->block_type == ONLY_SHORT_WINDOW) ? 3 : 5;
    int g;

    for (g = 0; g < coder->groups.n; g++) {
        int base = g * coder->sfbn;
        int end  = base + coder->sfbn;
        int b = base;
        if (b >= end) continue;

        /* Scan each section once (run, plus cost under its book (sumB) and its
           pair partner (sumA)), carrying the successor forward: one sweep. */
        int bookA = coder->book[b];
        int runA = 0, sumBA = 0, sumAA = 0;
        while (b + runA < end && coder->book[b + runA] == bookA) {
            sumBA += coder->bandmeta[b + runA].bits;
            sumAA += coder->bandmeta[b + runA].altbits;
            runA++;
        }

        while (b < end) {
            int c = b + runA;
            int merged = 0;

            if (bookA >= HCB_1 && bookA <= HCB_ESC && c < end) {
                int bookB = coder->book[c];
                int runB = 0, sumBB = 0, sumAB = 0;
                while (c + runB < end && coder->book[c + runB] == bookB) {
                    sumBB += coder->bandmeta[c + runB].bits;
                    sumAB += coder->bandmeta[c + runB].altbits;
                    runB++;
                }
                int hi = c + runB, k;

                if (bookB >= HCB_1 && bookB <= HCB_ESC && bookB != bookA) {
                    /* Target pair = the higher of the two books' pairs (each book
                       already bounds its range), so no rescan; only the lower-range
                       section, if any, is re-sized. */
                    int pairA = (bookA - 1) >> 1;
                    int pairB = (bookB - 1) >> 1;
                    int tgt_base = (pairA >= pairB ? pairA : pairB) * 2 + 1; /* pair 5 -> HCB_ESC */
                    int m0 = tgt_base;
                    int m1 = (tgt_base == HCB_ESC) ? tgt_base : tgt_base + 1;
                    int sized_lo, sized_hi;
                    if (pairA == pairB)      { sized_lo = sized_hi = b; }  /* nothing to size */
                    else if (pairA > pairB)  { sized_lo = c; sized_hi = hi; }
                    else                     { sized_lo = b; sized_hi = c; }

                    /* Only worth trying for a small, narrow mismatched section. */
                    int sized_lines = 0, sj;
                    for (sj = sized_lo; sj < sized_hi; sj++)
                        sized_lines += band_length(coder, sj);

                    if ((sized_hi - sized_lo) <= 2 && sized_lines <= CROSS_MERGE_MAX_LINES) {
                        /* Cost under each member = free section(s) from the sums
                           (book==m0 ? sumB : sumA) + the re-sized section. */
                        int cost0, cost1, size0 = 0, size1 = 0;
                        for (sj = sized_lo; sj < sized_hi; sj++) {
                            size0 += band_cost(coder, sj, m0);
                            size1 += (m1 == m0) ? 0 : band_cost(coder, sj, m1);
                        }
                        /* Free-section contribution to m0 (and m1 = its complement). */
                        int freeB0 = (sized_lo == c) ? 0 : ((bookA == m0) ? sumBA : sumAA);
                        int freeB1 = (sized_lo == c) ? 0 : ((bookA == m0) ? sumAA : sumBA);
                        int freeA0 = (sized_lo == b) ? 0 : ((bookB == m0) ? sumBB : sumAB);
                        int freeA1 = (sized_lo == b) ? 0 : ((bookB == m0) ? sumAB : sumBB);
                        cost0 = freeB0 + freeA0 + size0;
                        cost1 = (m1 == m0) ? cost0 : freeB1 + freeA1 + size1;

                        int best = (cost1 < cost0) ? m1 : m0;
                        int best_cost = (cost1 < cost0) ? cost1 : cost0;
                        int old_bits = sumBA + sumBB;
                        int hdr_delta = run_header_bits(hi - b, max_run, run_bits)
                                      - run_header_bits(runA, max_run, run_bits)
                                      - run_header_bits(runB, max_run, run_bits);

                        if (hdr_delta + best_cost - old_bits < 0) {
                            int other = (best == m0) ? m1 : m0;
                            for (k = b; k < hi; k++) {
                                int oldbook = coder->book[k];
                                coder->book[k] = best;
                                if (k >= sized_lo && k < sized_hi) {
                                    coder->bandmeta[k].bits    = band_cost(coder, k, best);
                                    coder->bandmeta[k].altbits = (other == best) ? coder->bandmeta[k].bits
                                                               : band_cost(coder, k, other);
                                } else if (oldbook != best) {
                                    /* free-section band flipped members: swap costs. */
                                    int t = coder->bandmeta[k].bits;
                                    coder->bandmeta[k].bits = coder->bandmeta[k].altbits;
                                    coder->bandmeta[k].altbits = t;
                                }
                            }
                            /* Merged run's totals are already known: update in
                               O(1) and re-evaluate from b so it can chain. */
                            bookA = best;
                            runA = hi - b;
                            sumBA = best_cost;
                            sumAA = (best == m0) ? cost1 : cost0;  /* cost under the other member */
                            merged = 1;
                        }
                    }
                }

                if (!merged) {
                    /* Advance; the successor's scan becomes the new section A. */
                    b = c; bookA = bookB;
                    runA = runB; sumBA = sumBB; sumAA = sumAB;
                }
            } else {
                /* A is non-coded or last: advance and scan the next section. */
                b = c;
                if (b < end) {
                    bookA = coder->book[b];
                    runA = 0; sumBA = 0; sumAA = 0;
                    while (b + runA < end && coder->book[b + runA] == bookA) {
                        sumBA += coder->bandmeta[b + runA].bits;
                        sumAA += coder->bandmeta[b + runA].altbits;
                        runA++;
                    }
                }
            }
        }
    }
}

/* Serialize the deferred codewords into coder->s[] in band order, using each
   band's final (post-merge) book. */
void SerializeSpectralData(CoderInfo *coder)
{
    int b;

    coder->datacnt = 0;
    for (b = 0; b < coder->bandcnt; b++) {
        int book = coder->book[b];
        if (book < HCB_1 || book > HCB_ESC) continue;   /* ZERO/PNS/IS/NONE */

        huffcode_write(coder->quant + coder->bandmeta[b].off,
                       band_length(coder, b), book, coder);
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

    for (g = 0; g < coder->groups.n; g++) {
        int b = g * coder->sfbn;
        int end = b + coder->sfbn;
        while (b < end) {
            int book = coder->book[b];
            int run = 0;
            while (b + run < end && coder->book[b + run] == book) run++;
            b += run;

            if (write) PutBit(stream, book, 4);
            bits += 4;

            while (run >= max_run) {
                if (write) PutBit(stream, max_run, run_bits);
                bits += run_bits;
                run -= max_run;
            }
            if (write) PutBit(stream, run, run_bits);
            bits += run_bits;
        }
    }
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
                if (write) PutBit(stream, diff + 256, 9);
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
        if (write) PutBit(stream, code, len);
        bits += len;
    }
    return bits;
}
