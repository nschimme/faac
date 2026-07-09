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
#include <string.h>
#include "coder.h"
#include "huffdata.h"
#include "huff2.h"
#include "bitstream.h"
#include "util.h"

/* Huffman book pointer mapping for ISO/IEC 14496-3 indices */
static hcode16_t * const hmap[12] = {
    NULL, book01, book02, book03, book04, book05,
    book06, book07, book08, book09, book10, book11
};

/* Escape coding for HCB_ESC as per ISO/IEC 14496-3. */
static int escape(int x, int *code)
{
    int preflen = 31 - CountLeadingZeros(x) - 4;
    int base = 1 << (preflen + 4);

    if (code) {
        /* Unary prefix: preflen 1s followed by a 0, then the (preflen+4) bit suffix */
        *code = (((1 << (preflen + 1)) - 2) << (preflen + 4)) | (x - base);
    }

    return (preflen << 1) + 5;
}

/* Unified Huffman bit-length sizing and writing logic. Drives tuple, sign, and
   escape logic via book-number properties to eliminate code duplication while
   preserving O(1) decision overhead in the encoding loop. */
int huffcode_op(int16_t *qs, int len, int bnum, CoderInfo *coder)
{
    hcode16_t *book = hmap[bnum];
    int i, bits = 0;
    int datacnt = coder ? coder->datacnt : 0;
    int step = (bnum <= HCB_4) ? 4 : 2;

    for (i = 0; i < len; i += step) {
        int idx, blen, data, j;
        int v0 = abs(qs[i]), v1 = abs(qs[i+1]);

        if (bnum <= HCB_2) {
            /* Signed 4-tuples (books 1, 2) */
            idx = 40 + DIM_S4*DIM_S4*DIM_S4*qs[i] + DIM_S4*DIM_S4*qs[i+1] + DIM_S4*qs[i+2] + qs[i+3];
            blen = book[idx].len;
            data = book[idx].data;
        } else if (bnum <= HCB_4) {
            /* Unsigned 4-tuples (books 3, 4) */
            idx = DIM_M4*DIM_M4*DIM_M4*v0 + DIM_M4*DIM_M4*v1 + DIM_M4*abs(qs[i+2]) + abs(qs[i+3]);
            blen = book[idx].len;
            data = book[idx].data;
        } else if (bnum <= HCB_6) {
            /* Signed 2-tuples (books 5, 6) */
            idx = 40 + DIM_S2*qs[i] + qs[i+1];
            blen = book[idx].len;
            data = book[idx].data;
        } else if (bnum == HCB_ESC) {
            /* Unsigned 2-tuples with escapes (book 11) */
            idx = DIM_ESC * (v0 > LAV_ESC ? LAV_ESC : v0) + (v1 > LAV_ESC ? LAV_ESC : v1);
            blen = book[idx].len;
            data = book[idx].data;
        } else {
            /* Unsigned 2-tuples (books 7-10) */
            idx = (bnum >= HCB_9 ? DIM_M2_12 : DIM_M2_7) * v0 + v1;
            blen = book[idx].len;
            data = book[idx].data;
        }

        /* Append sign bits for unsigned books (excluding signed books 1, 2, 5, 6) */
        if (bnum >= HCB_3 && bnum != HCB_5 && bnum != HCB_6) {
            for (j = 0; j < step; j++) {
                if (qs[i+j]) {
                    blen++;
                    data = (data << 1) | (qs[i+j] < 0);
                }
            }
        }

        if (coder) {
            coder->s[datacnt].data = data;
            coder->s[datacnt++].len = blen;
        }
        bits += blen;

        /* Append escape suffixes for HCB_ESC */
        if (bnum == HCB_ESC) {
            for (j = 0; j < 2; j++) {
                int val = abs(qs[i+j]);
                if (val >= LAV_ESC) {
                    int esc_code, esc_len = escape(val, &esc_code);
                    if (coder) {
                        coder->s[datacnt].data = esc_code;
                        coder->s[datacnt++].len = esc_len;
                    }
                    bits += esc_len;
                }
            }
        }
    }

    if (coder) {
        coder->datacnt = datacnt;
    }
    return bits;
}

/* Defer serialization: record band metadata and choose the initial book. */
int huffbook(CoderInfo *coder, CoderScratch *scratch, int *qs, int len, int maxq)
{
    int band = coder->bandcnt;

    if (maxq > 0) {
        int i, pair_base, altlen, bookmin;
        int16_t *p_quant = scratch->quant + scratch->quantcnt;

        for (i = 0; i < len; i++) {
            p_quant[i] = (int16_t)qs[i];
        }

        /* Select range-pair whose lower book just fits maxq. */
        if (maxq <= LAV_1)      pair_base = HCB_1;
        else if (maxq <= LAV_2) pair_base = HCB_3;
        else if (maxq <= LAV_4) pair_base = HCB_5;
        else if (maxq <= LAV_7) pair_base = HCB_7;
        else if (maxq <= LAV_12) pair_base = HCB_9;
        else pair_base = HCB_ESC;

        if (pair_base != HCB_ESC) {
            int len1 = huffcode_op(p_quant, len, pair_base, NULL);
            int len2 = huffcode_op(p_quant, len, pair_base + 1, NULL);
            if (len2 < len1) {
                bookmin = pair_base + 1;
                altlen = len1;
            } else {
                bookmin = pair_base;
                altlen = len2;
            }
        } else {
            bookmin = HCB_ESC;
            altlen = huffcode_op(p_quant, len, HCB_ESC, NULL);
        }

        scratch->bandmeta[band].off = (uint16_t)scratch->quantcnt;
        scratch->bandmeta[band].bits = (uint16_t)huffcode_op(p_quant, len, bookmin, NULL);
        scratch->bandmeta[band].altbits = (uint16_t)altlen;
        scratch->quantcnt += len;
        coder->book[band] = bookmin;
    } else {
        coder->book[band] = HCB_ZERO;
    }

    return 0;
}

static int band_length(const CoderInfo *coder, int b)
{
    int g = b / coder->sfbn;
    int sb = b % coder->sfbn;
    return coder->groups.len[g] * (coder->sfb_offset[sb + 1] - coder->sfb_offset[sb]);
}

static inline int run_header_bits(int run, int max_run, int run_bits)
{
    return 4 + run_bits * (run / max_run + 1);
}

/* Fuse adjacent Huffman sections when bitstream header savings beat codeword bits. */
void MergeSections(CoderInfo *coder, CoderScratch *scratch)
{
    int max_run = (coder->block_type == ONLY_SHORT_WINDOW) ? 7 : 31;
    int run_bits = (coder->block_type == ONLY_SHORT_WINDOW) ? 3 : 5;
    int g;

    for (g = 0; g < coder->groups.n; g++) {
        int base = g * coder->sfbn;
        int end  = base + coder->sfbn;
        int b = base;
        if (b >= end) continue;

        int bookA = coder->book[b], runA = 0, sumBA = 0, sumAA = 0;
        while (b + runA < end && coder->book[b + runA] == bookA) {
            if (bookA >= HCB_1 && bookA <= HCB_ESC) {
                sumBA += scratch->bandmeta[b + runA].bits;
                sumAA += scratch->bandmeta[b + runA].altbits;
            }
            runA++;
        }

        while (b < end) {
            int c = b + runA, merged = 0;
            if (bookA >= HCB_1 && bookA <= HCB_ESC && c < end) {
                int bookB = coder->book[c], runB = 0, sumBB = 0, sumAB = 0;
                while (c + runB < end && coder->book[c + runB] == bookB) {
                    if (bookB >= HCB_1 && bookB <= HCB_ESC) {
                        sumBB += scratch->bandmeta[c + runB].bits;
                        sumAB += scratch->bandmeta[c + runB].altbits;
                    }
                    runB++;
                }

                if (bookB >= HCB_1 && bookB <= HCB_ESC && bookB != bookA) {
                    int hi = c + runB;
                    int m0 = ((bookA - 1) / 2 >= (bookB - 1) / 2 ? (bookA - 1) / 2 : (bookB - 1) / 2) * 2 + 1;
                    int m1 = (m0 == HCB_ESC) ? m0 : m0 + 1;
                    int sized_lo = ((bookA - 1) / 2 == (bookB - 1) / 2) ? b : (((bookA - 1) / 2 > (bookB - 1) / 2) ? c : b);
                    int sized_hi = (sized_lo == b) ? c : hi;
                    int sized_lines = 0, sj;

                    for (sj = sized_lo; sj < sized_hi; sj++) {
                        sized_lines += band_length(coder, sj);
                    }

                    if ((sized_hi - sized_lo) <= 2 && sized_lines <= CROSS_MERGE_MAX_LINES) {
                        int cost0, cost1, size0 = 0, size1 = 0;
                        for (sj = sized_lo; sj < sized_hi; sj++) {
                            size0 += huffcode_op(scratch->quant + scratch->bandmeta[sj].off, band_length(coder, sj), m0, NULL);
                            size1 += (m1 == m0) ? 0 : huffcode_op(scratch->quant + scratch->bandmeta[sj].off, band_length(coder, sj), m1, NULL);
                        }
                        cost0 = (sized_lo == c ? ((bookA == m0) ? sumBA : sumAA) : 0) + (sized_lo == b ? ((bookB == m0) ? sumBB : sumAB) : 0) + size0;
                        cost1 = (m1 == m0) ? cost0 : (sized_lo == c ? ((bookA == m1) ? sumBA : sumAA) : 0) + (sized_lo == b ? ((bookB == m1) ? sumBB : sumAB) : 0) + size1;

                        int best = (cost1 < cost0) ? m1 : m0;
                        int best_cost = (cost1 < cost0) ? cost1 : cost0;
                        int hdr_delta = run_header_bits(hi - b, max_run, run_bits) - run_header_bits(runA, max_run, run_bits) - run_header_bits(runB, max_run, run_bits);

                        if (hdr_delta + best_cost - (sumBA + sumBB) < 0) {
                            int other = (best == m0) ? m1 : m0, k;
                            for (k = b; k < hi; k++) {
                                int oldbook = coder->book[k];
                                coder->book[k] = best;
                                if (k >= sized_lo && k < sized_hi) {
                                    scratch->bandmeta[k].bits = (uint16_t)huffcode_op(scratch->quant + scratch->bandmeta[k].off, band_length(coder, k), best, NULL);
                                    scratch->bandmeta[k].altbits = (uint16_t)((other == best) ? scratch->bandmeta[k].bits : huffcode_op(scratch->quant + scratch->bandmeta[k].off, band_length(coder, k), other, NULL));
                                } else if (oldbook != best) {
                                    uint16_t t = scratch->bandmeta[k].bits;
                                    scratch->bandmeta[k].bits = scratch->bandmeta[k].altbits;
                                    scratch->bandmeta[k].altbits = t;
                                }
                            }
                            bookA = best; runA = hi - b; sumBA = best_cost; sumAA = (best == m0) ? cost1 : cost0; merged = 1;
                        }
                    }
                }
                if (!merged) { b = c; bookA = bookB; runA = runB; sumBA = sumBB; sumAA = sumAB; }
            } else {
                b = c;
                if (b < end) {
                    bookA = coder->book[b]; runA = 0; sumBA = 0; sumAA = 0;
                    while (b + runA < end && coder->book[b + runA] == bookA) {
                        if (bookA >= HCB_1 && bookA <= HCB_ESC) {
                            sumBA += scratch->bandmeta[b + runA].bits;
                            sumAA += scratch->bandmeta[b + runA].altbits;
                        }
                        runA++;
                    }
                }
            }
        }
    }
}

void SerializeSpectralData(CoderInfo *coder, CoderScratch *scratch)
{
    int b;
    coder->datacnt = 0;
    for (b = 0; b < coder->bandcnt; b++) {
        if (coder->book[b] >= HCB_1 && coder->book[b] <= HCB_ESC) {
            huffcode_op(scratch->quant + scratch->bandmeta[b].off, band_length(coder, b), coder->book[b], coder);
        }
    }
}

int writebooks(CoderInfo *coder, BitStream *stream, int write)
{
    int bits = 0;
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
