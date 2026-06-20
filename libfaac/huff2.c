/****************************************************************************
    Huffman coding

    Copyright (C) 2017 Krzysztof Nikiel

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include "coder.h"
#include "huffdata.h"
#include "huff2.h"
#include "bitstream.h"

#ifdef __GNUC__
#define clz(x) __builtin_clz(x)
#else
static int clz(unsigned int x)
{
    int n = 0;
    if (x == 0) return 32;
    if (x <= 0x0000FFFF) { n += 16; x <<= 16; }
    if (x <= 0x00FFFFFF) { n += 8; x <<= 8; }
    if (x <= 0x0FFFFFFF) { n += 4; x <<= 4; }
    if (x <= 0x3FFFFFFF) { n += 2; x <<= 2; }
    if (x <= 0x7FFFFFFF) { n += 1; x <<= 1; }
    return n;
}
#endif

/* Escape suffix for book 11: a magnitude |q| >= 16 is sent as the pair index 16
 * (emitted by the caller) plus this suffix - a unary prefix of `preflen` ones and
 * a zero, then the low `preflen+4` bits of x. preflen counts how far x is past the
 * 16-window, so the suffix is 2*preflen+5 bits. */
static int escape(int x, int *code)
{
    int preflen;
    int base;

    if (x > MAX_HUFF_ESC_VAL)
    {
        fprintf(stderr, "%s(%d): x_quant > %d\n", __FILE__, __LINE__, MAX_HUFF_ESC_VAL);
        return 0;
    }

    preflen = 31 - clz(x) - 4;
    base = 1 << (preflen + 4);

    *code = (1 << (preflen + 1)) - 2; // preflen 1s and a 0
    *code <<= (preflen + 4);
    *code |= (x - base);

    return (preflen << 1) + 5;
}

/* Code `len` coeffs under book `bnum`, returning bit count. coder == NULL only
 * sizes (the hot cost path); else also emits codewords to coder->s[]. len/code
 * tables are split (huff_len/huff_data, sliced by huff_offset) so the cost path
 * streams uint8 lengths only. Signed books (1,2,5,6) bias the index by 40 and
 * fold in sign; magnitude books append explicit sign bits. */
static int huffcode(int *qs /* quantized spectrum */,
                    int len,
                    int bnum,
                    CoderInfo *coder)
{
    const uint8_t *blens = huff_len + huff_offset[bnum - 1];
    const uint16_t *bdata = huff_data + huff_offset[bnum - 1];
    int bits = 0, blen;
    int ofs, *qp;
    int data = 0;
    int idx;
    int datacnt;

    if (coder)
        datacnt = coder->datacnt;
    else
        datacnt = 0;

    switch (bnum)
    {
    case 1:
    case 2:
        for(ofs = 0; ofs < len; ofs += 4)
        {
            int q0, q1, q2, q3;
            if (len - ofs >= 4) {
                qp = qs + ofs;
                q0 = qp[0]; q1 = qp[1]; q2 = qp[2]; q3 = qp[3];
            } else {
                int q[4] = {0};
                for (int j = 0; j < len - ofs; j++) q[j] = qs[ofs + j];
                q0 = q[0]; q1 = q[1]; q2 = q[2]; q3 = q[3];
            }

            if (!(q0 | q1 | q2 | q3)) {
                blen = blens[40];
                if (coder) {
                    coder->s[datacnt].data = bdata[40];
                    coder->s[datacnt++].len = blen;
                }
                bits += blen;
                continue;
            }
            idx = 27 * q0 + 9 * q1 + 3 * q2 + q3 + 40;
            if (idx < 0 || idx >= 81) return -1;
            blen = blens[idx];
            if (coder)
            {
                data = bdata[idx];
                coder->s[datacnt].data = data;
                coder->s[datacnt++].len = blen;
            }
            bits += blen;
        }
        break;
    case 3:
    case 4:
        for(ofs = 0; ofs < len; ofs += 4)
        {
            int q0, q1, q2, q3;
            if (len - ofs >= 4) {
                qp = qs + ofs;
                q0 = qp[0]; q1 = qp[1]; q2 = qp[2]; q3 = qp[3];
            } else {
                int q[4] = {0};
                for (int j = 0; j < len - ofs; j++) q[j] = qs[ofs + j];
                q0 = q[0]; q1 = q[1]; q2 = q[2]; q3 = q[3];
            }

            if (!(q0 | q1 | q2 | q3)) {
                blen = blens[0];
                if (coder) {
                    coder->s[datacnt].data = bdata[0];
                    coder->s[datacnt++].len = blen;
                }
                bits += blen;
                continue;
            }
            int aq0 = abs(q0), aq1 = abs(q1), aq2 = abs(q2), aq3 = abs(q3);
            idx = 27 * aq0 + 9 * aq1 + 3 * aq2 + aq3;
            if (idx < 0 || idx >= 81) return -1;
            blen = blens[idx];
            if (!coder)
            {
                if(q0) blen++;
                if(q1) blen++;
                if(q2) blen++;
                if(q3) blen++;
            }
            else
            {
                data = bdata[idx];
                if(q0) { blen++; data <<= 1; if (q0 < 0) data |= 1; }
                if(q1) { blen++; data <<= 1; if (q1 < 0) data |= 1; }
                if(q2) { blen++; data <<= 1; if (q2 < 0) data |= 1; }
                if(q3) { blen++; data <<= 1; if (q3 < 0) data |= 1; }
                coder->s[datacnt].data = data;
                coder->s[datacnt++].len = blen;
            }
            bits += blen;
        }
        break;
    case 5:
    case 6:
        for(ofs = 0; ofs < len; ofs += 2)
        {
            int q0, q1;
            if (len - ofs >= 2) {
                qp = qs + ofs;
                q0 = qp[0]; q1 = qp[1];
            } else {
                q0 = qs[ofs]; q1 = 0;
            }

            if (!(q0 | q1)) {
                blen = blens[40];
                if (coder) {
                    coder->s[datacnt].data = bdata[40];
                    coder->s[datacnt++].len = blen;
                }
                bits += blen;
                continue;
            }
            idx = 9 * q0 + q1 + 40;
            if (idx < 0 || idx >= 81) return -1;
            blen = blens[idx];
            if (coder)
            {
                data = bdata[idx];
                coder->s[datacnt].data = data;
                coder->s[datacnt++].len = blen;
            }
            bits += blen;
        }
        break;
    case 7:
    case 8:
        for(ofs = 0; ofs < len; ofs += 2)
        {
            int q0, q1;
            if (len - ofs >= 2) {
                qp = qs + ofs;
                q0 = qp[0]; q1 = qp[1];
            } else {
                q0 = qs[ofs]; q1 = 0;
            }

            if (!(q0 | q1)) {
                blen = blens[0];
                if (coder) {
                    coder->s[datacnt].data = bdata[0];
                    coder->s[datacnt++].len = blen;
                }
                bits += blen;
                continue;
            }
            int aq0 = abs(q0), aq1 = abs(q1);
            idx = 8 * aq0 + aq1;
            if (idx < 0 || idx >= 64) return -1;
            blen = blens[idx];
            if (!coder)
            {
                if(q0) blen++;
                if(q1) blen++;
            }
            else
            {
                data = bdata[idx];
                if(q0) { blen++; data <<= 1; if (q0 < 0) data |= 1; }
                if(q1) { blen++; data <<= 1; if (q1 < 0) data |= 1; }
                coder->s[datacnt].data = data;
                coder->s[datacnt++].len = blen;
            }
            bits += blen;
        }
        break;
    case 9:
    case 10:
        for(ofs = 0; ofs < len; ofs += 2)
        {
            int q0, q1;
            if (len - ofs >= 2) {
                qp = qs + ofs;
                q0 = qp[0]; q1 = qp[1];
            } else {
                q0 = qs[ofs]; q1 = 0;
            }

            if (!(q0 | q1)) {
                blen = blens[0];
                if (coder) {
                    coder->s[datacnt].data = bdata[0];
                    coder->s[datacnt++].len = blen;
                }
                bits += blen;
                continue;
            }
            int aq0 = abs(q0), aq1 = abs(q1);
            idx = 13 * aq0 + aq1;
            if (idx < 0 || idx >= 169) return -1;
            blen = blens[idx];
            if (!coder)
            {
                if(q0) blen++;
                if(q1) blen++;
            }
            else
            {
                data = bdata[idx];
                if(q0) { blen++; data <<= 1; if (q0 < 0) data |= 1; }
                if(q1) { blen++; data <<= 1; if (q1 < 0) data |= 1; }
                coder->s[datacnt].data = data;
                coder->s[datacnt++].len = blen;
            }
            bits += blen;
        }
        break;
    case HCB_ESC:
        for(ofs = 0; ofs < len; ofs += 2)
        {
            int q0, q1, aq0, aq1, x0, x1;
            if (len - ofs >= 2) {
                qp = qs + ofs;
                q0 = qp[0]; q1 = qp[1];
            } else {
                q0 = qs[ofs]; q1 = 0;
            }

            if (!(q0 | q1)) {
                blen = blens[0];
                if (coder) {
                    coder->s[datacnt].data = bdata[0];
                    coder->s[datacnt++].len = blen;
                }
                bits += blen;
                continue;
            }
            aq0 = abs(q0); aq1 = abs(q1);

            x0 = (aq0 > 16) ? 16 : aq0;
            x1 = (aq1 > 16) ? 16 : aq1;
            idx = 17 * x0 + x1;
            if (idx < 0 || idx >= 289) return -1;

            blen = blens[idx];
            if (!coder)
            {
                if(q0) blen++;
                if(q1) blen++;
            }
            else
            {
                data = bdata[idx];
                if(q0) { blen++; data <<= 1; if (q0 < 0) data |= 1; }
                if(q1) { blen++; data <<= 1; if (q1 < 0) data |= 1; }
                coder->s[datacnt].data = data;
                coder->s[datacnt++].len = blen;
            }
            bits += blen;

            if (aq0 >= 16)
            {
                blen = escape(aq0, &data);
                if (coder)
                {
                    coder->s[datacnt].data = data;
                    coder->s[datacnt++].len = blen;
                }
                bits += blen;
            }

            if (aq1 >= 16)
            {
                blen = escape(aq1, &data);
                if (coder)
                {
                    coder->s[datacnt].data = data;
                    coder->s[datacnt++].len = blen;
                }
                bits += blen;
            }
        }
        break;
    default:
        fprintf(stderr, "%s(%d) book %d out of range\n", __FILE__, __LINE__, bnum);
        return -1;
    }

    if (coder)
        coder->datacnt = datacnt;

    return bits;
}


/* Lowest base codebook whose range-pair covers |maxq| (keeps the decoder in
 * range, cf. the escape/out-of-range guards). Returns the lower book of the
 * pair; HCB_ZERO for an empty band, HCB_ESC for the escape book. Shared by
 * huffbook() (per-band selection) and section_optimize() (merge re-costing). */
static int book_for_maxq(int maxq)
{
    if (maxq < 1)  return HCB_ZERO;
    if (maxq < 2)  return 1;
    if (maxq < 3)  return 3;
    if (maxq < 5)  return 5;
    if (maxq < 8)  return 7;
    if (maxq < 13) return 9;
    return HCB_ESC;
}

/* Bit cost of coding [qs,len) under BOTH books of a range-pair in one traversal.
 * `base` is the lower book (1,3,5,7,9 — never ESC, which has no pair). The two
 * books of a pair share the per-tuple index and sign accounting and differ only
 * in the length table, so we look up both lengths per tuple and avoid a second
 * pass. Results match two huffcode(...,0) count calls exactly. */
static void huff_count_pair(const int *qs, int len, int base, int *pc0, int *pc1)
{
    const uint8_t *la = huff_len + huff_offset[base - 1];
    const uint8_t *lb = huff_len + huff_offset[base];
    int c0 = 0, c1 = 0, ofs;

    switch (base)
    {
    case 1:   /* books 1,2: signed 4-tuple, no sign bits */
        for (ofs = 0; ofs < len; ofs += 4)
        {
            int q0, q1 = 0, q2 = 0, q3 = 0, idx;
            q0 = qs[ofs];
            if (len - ofs > 1) q1 = qs[ofs+1];
            if (len - ofs > 2) q2 = qs[ofs+2];
            if (len - ofs > 3) q3 = qs[ofs+3];
            idx = (!(q0 | q1 | q2 | q3)) ? 40 : 27*q0 + 9*q1 + 3*q2 + q3 + 40;
            c0 += la[idx]; c1 += lb[idx];
        }
        break;
    case 3:   /* books 3,4: magnitude 4-tuple + sign bits */
        for (ofs = 0; ofs < len; ofs += 4)
        {
            int q0, q1 = 0, q2 = 0, q3 = 0, idx, s;
            q0 = qs[ofs];
            if (len - ofs > 1) q1 = qs[ofs+1];
            if (len - ofs > 2) q2 = qs[ofs+2];
            if (len - ofs > 3) q3 = qs[ofs+3];
            if (!(q0 | q1 | q2 | q3)) { c0 += la[0]; c1 += lb[0]; continue; }
            idx = 27*abs(q0) + 9*abs(q1) + 3*abs(q2) + abs(q3);
            s = (q0 != 0) + (q1 != 0) + (q2 != 0) + (q3 != 0);
            c0 += la[idx] + s; c1 += lb[idx] + s;
        }
        break;
    case 5:   /* books 5,6: signed 2-tuple, no sign bits */
        for (ofs = 0; ofs < len; ofs += 2)
        {
            int q0, q1 = 0, idx;
            q0 = qs[ofs];
            if (len - ofs > 1) q1 = qs[ofs+1];
            idx = (!(q0 | q1)) ? 40 : 9*q0 + q1 + 40;
            c0 += la[idx]; c1 += lb[idx];
        }
        break;
    case 7:   /* books 7,8: magnitude 2-tuple + sign bits, idx = 8*aq0 + aq1 */
        for (ofs = 0; ofs < len; ofs += 2)
        {
            int q0, q1 = 0, idx, s;
            q0 = qs[ofs];
            if (len - ofs > 1) q1 = qs[ofs+1];
            if (!(q0 | q1)) { c0 += la[0]; c1 += lb[0]; continue; }
            idx = 8*abs(q0) + abs(q1);
            s = (q0 != 0) + (q1 != 0);
            c0 += la[idx] + s; c1 += lb[idx] + s;
        }
        break;
    default:  /* base == 9: books 9,10: magnitude 2-tuple + sign, idx = 13*aq0 + aq1 */
        for (ofs = 0; ofs < len; ofs += 2)
        {
            int q0, q1 = 0, idx, s;
            q0 = qs[ofs];
            if (len - ofs > 1) q1 = qs[ofs+1];
            if (!(q0 | q1)) { c0 += la[0]; c1 += lb[0]; continue; }
            idx = 13*abs(q0) + abs(q1);
            s = (q0 != 0) + (q1 != 0);
            c0 += la[idx] + s; c1 += lb[idx] + s;
        }
        break;
    }

    *pc0 = c0;
    *pc1 = c1;
}

/* Select the cheapest codebook for one band and cache its spectral bit cost in
 * coder->blen[]; symbols are NOT emitted here (deferred to emit_spectral after
 * section_optimize has finalised the books). Only the band's own tier range-pair
 * is costed (cost_pair[]), which is all section_optimize needs to sum same-tier
 * spans for free; cross-tier spans recost on demand. */
int huffbook(CoderInfo *coder,
             int *qs /* quantized spectrum */,
             int len)
{
    int bandcnt = coder->bandcnt;
    int *cp = coder->cost_pair[bandcnt];
    int maxq = 0;
    int base, i;

    if (len == 0)
    {
        coder->book[bandcnt] = HCB_ZERO;
        coder->blen[bandcnt] = 0;
        cp[0] = cp[1] = 0;
        return 0;
    }

    for (i = 0; i < len; i++)
    {
        int aq = abs(qs[i]);
        if (aq > maxq) maxq = aq;
    }

    base = book_for_maxq(maxq);
    if (base == HCB_ZERO)
    {
        coder->book[bandcnt] = HCB_ZERO;
        coder->blen[bandcnt] = 0;
        cp[0] = cp[1] = 0;
    }
    else if (base == HCB_ESC)
    {
        int c = huffcode(qs, len, HCB_ESC, 0);
        coder->book[bandcnt] = HCB_ESC;
        coder->blen[bandcnt] = c;
        cp[0] = cp[1] = c;   /* ESC tier has no pair partner */
    }
    else
    {
        /* range-pair: keep whichever of {base, base+1} codes the band shorter */
        int c0, c1;
        huff_count_pair(qs, len, base, &c0, &c1);
        cp[0] = c0;
        cp[1] = c1;
        if (c1 < c0)
        {
            coder->book[bandcnt] = base + 1;
            coder->blen[bandcnt] = c1;
        }
        else
        {
            coder->book[bandcnt] = base;
            coder->blen[bandcnt] = c0;
        }
    }

    return 0;
}

/* Range tier of a spectral book (higher tier covers larger |coef|); 0 for
 * HCB_ZERO and non-spectral books. */
static int book_tier(int book)
{
    switch (book)
    {
    case 1: case 2:  return 1;
    case 3: case 4:  return 2;
    case 5: case 6:  return 3;
    case 7: case 8:  return 4;
    case 9: case 10: return 5;
    case HCB_ESC:    return 6;
    default:         return 0;
    }
}

static int tier_base_book(int tier)
{
    switch (tier)
    {
    case 1: return 1;
    case 2: return 3;
    case 3: return 5;
    case 4: return 7;
    case 5: return 9;
    case 6: return HCB_ESC;
    default: return HCB_ZERO;
    }
}

/* A band carries Huffman-coded spectral data iff its book is in [1, HCB_ESC].
 * HCB_ZERO/PNS/intensity carry none and act as hard section boundaries. */
static int is_spectral(int book)
{
    return book >= 1 && book <= HCB_ESC;
}

/* Sentinel cost for a range-pair partner that doesn't exist (ESC tier). Far
 * above any real span cost, so min() never picks it; never added to. */
#define COST_INF (1 << 28)

/* Greedy section merge ("codebook hysteresis"): per group, absorb the next
 * spectral band into the open section whenever recoding the span under a shared
 * covering book costs fewer extra bits than the section header it saves. Lossless
 * - only reassigns spectral books, never touching scalefactors or band class.
 * Costed lazily: run0/run1 hold the span's cost under the covering tier's
 * base/base+1; a same-tier band adds its cached cost_pair[] for free, a cross-tier
 * one recosts only the contiguous qspec slice that changed. */
void section_optimize(CoderInfo *coder)
{
    int shortwin = (coder->block_type == ONLY_SHORT_WINDOW);
    int maxcnt = shortwin ? 7 : 31;   /* section run length writebooks() can encode */
    int header = 4 + (shortwin ? 3 : 5); /* bits saved by dropping one section header */
    int g;

    for (g = 0; g < coder->groups.n; g++)
    {
        int band = g * coder->sfbn;
        int maxband = band + coder->sfbn;

        while (band < maxband)
        {
            int sec_start, sec_tier, sec_spec;
            int sec_ofs, sec_len;
            int run0, run1;   /* span cost under covering base / base+1 */

            if (!is_spectral(coder->book[band]))
            {
                band++;
                continue;
            }

            sec_start = band;
            sec_tier  = book_tier(coder->book[band]);
            sec_ofs   = coder->qoffset[band];
            sec_len   = coder->qlen[band];
            sec_spec  = coder->blen[band];
            run0 = coder->cost_pair[band][0];
            run1 = (sec_tier < 6) ? coder->cost_pair[band][1] : COST_INF;
            band++;

            /* Cap the run at maxcnt so the saved-header model stays exact. */
            while (band < maxband
                   && is_spectral(coder->book[band])
                   && (band - sec_start) < maxcnt)
            {
                int t = book_tier(coder->book[band]);
                int cand_tier = (t > sec_tier) ? t : sec_tier;
                int ofs = coder->qoffset[band], ln = coder->qlen[band];
                int base, best_book, best_cost, k;
                int n0, n1;

                if (cand_tier == sec_tier)
                {
                    if (t == sec_tier)
                    {
                        /* band already costed under the covering pair: free */
                        n0 = run0 + coder->cost_pair[band][0];
                        n1 = (sec_tier < 6) ? run1 + coder->cost_pair[band][1]
                                            : COST_INF;
                    }
                    else
                    {
                        /* lower-tier band under the higher covering book: recost it */
                        int cbase = tier_base_book(sec_tier);
                        if (sec_tier < 6)
                        {
                            int a0, a1;
                            huff_count_pair(coder->qspec + ofs, ln, cbase, &a0, &a1);
                            n0 = run0 + a0;
                            n1 = run1 + a1;
                        }
                        else
                        {
                            n0 = run0 + huffcode(coder->qspec + ofs, ln, cbase, 0);
                            n1 = COST_INF;
                        }
                    }
                }
                else
                {
                    /* covering tier rises to t: recost the existing span under the
                     * new higher book; the raising band is already under its own
                     * (== new covering) pair. */
                    int nbase = tier_base_book(t);
                    if (t < 6)
                    {
                        int s0, s1;
                        huff_count_pair(coder->qspec + sec_ofs, sec_len, nbase, &s0, &s1);
                        n0 = s0 + coder->cost_pair[band][0];
                        n1 = s1 + coder->cost_pair[band][1];
                    }
                    else
                    {
                        n0 = huffcode(coder->qspec + sec_ofs, sec_len, nbase, 0)
                             + coder->cost_pair[band][0];
                        n1 = COST_INF;
                    }
                }

                base = tier_base_book(cand_tier);
                if (n1 < n0)
                {
                    best_cost = n1;
                    best_book = base + 1;
                }
                else
                {
                    best_cost = n0;
                    best_book = base;
                }

                if (best_cost - sec_spec - coder->blen[band] < header)
                {
                    for (k = sec_start; k <= band; k++)
                        coder->book[k] = best_book;
                    sec_tier = cand_tier;
                    sec_len += ln;
                    sec_spec = best_cost;
                    run0 = n0;
                    run1 = n1;
                    band++;
                }
                else
                {
                    break;
                }
            }
        }
    }
}

/* Emit Huffman symbols for all spectral bands in band order, filling coder->s[]
 * for WriteSpectralData(). Adjacent bands with the same book are combined into
 * a single huffcode call to maximize throughput. */
void emit_spectral(CoderInfo *coder)
{
    int b;

    coder->datacnt = 0;
    for (b = 0; b < coder->bandcnt; )
    {
        int book = coder->book[b];
        if (is_spectral(book))
        {
            int ofs = coder->qoffset[b];
            int len = coder->qlen[b];
            b++;
            while (b < coder->bandcnt && coder->book[b] == book)
            {
                len += coder->qlen[b];
                b++;
            }
            huffcode(coder->qspec + ofs, len, book, coder);
        }
        else
        {
            b++;
        }
    }
}

/* Write (or size) the section layout: 4-bit book + run length per maximal run of
 * equal adjacent books. The count field is cntbits wide (5 long / 3 short), so a
 * run past maxcnt splits into repeated full sections. section_optimize() grows the
 * runs; this just RLE-encodes the final books[]. */
int writebooks(CoderInfo *coder, BitStream *stream, int write)
{
    int bits = 0;
    int maxcnt, cntbits;
    int group;
    int bookbits = 4;

    if (coder->block_type == ONLY_SHORT_WINDOW){
        maxcnt = 7;
        cntbits = 3;
    } else {
        maxcnt = 31;
        cntbits = 5;
    }

    for (group = 0; group < coder->groups.n; group++)
    {
        int band = group * coder->sfbn;
        int maxband = band + coder->sfbn;

        while (band < maxband)
        {
            int book = coder->book[band++];
            int bookcnt = 1;
            if (write) {
                PutBit(stream, book, bookbits);
            }
            bits += bookbits;

            if (band < maxband)
            {
                while (book == coder->book[band])
                {
                    band++;
                    bookcnt++;
                    if (band >= maxband)
                        break;
                }
            }

            while (bookcnt >= maxcnt)
            {
                if (write)
                    PutBit(stream, maxcnt, cntbits);
                bits += cntbits;
                bookcnt -= maxcnt;
            }
            if (write)
                PutBit(stream, bookcnt, cntbits);
            bits += cntbits;
        }
    }

    return bits;
}

int writesf(CoderInfo *coder, BitStream *stream, int write)
{
    int cnt;
    int bits = 0;
    int diff, length;
    int lastsf;
    int lastis;
    int lastpns;
    int initpns = 1;

    /* Three independent DPCM chains share book 12, each seeded so its first delta
     * is self-contained: intensity positions from 0, scalefactors from global_gain
     * (itself the first regular sf), PNS energies from global_gain - SF_PNS_OFFSET.
     * The decoder rebuilds each by the same running sum, so deltas must match. */
    lastsf = coder->global_gain;
    lastis = 0;
    lastpns = coder->global_gain - SF_PNS_OFFSET;

    /* qlevel() bounds every stored sf[] and global_gain to [0, SF_MAX_ABS], so
     * the running reconstruction below cannot leave the decoder's range. */
    for (cnt = 0; cnt < coder->bandcnt; cnt++)
    {
        int book = coder->book[cnt];

        if ((book == HCB_INTENSITY) || (book== HCB_INTENSITY2))
        {
            diff = coder->sf[cnt] - lastis;
            diff = clamp_sf_diff(diff);
            length = book12_len[SF_DELTA + diff];

            bits += length;

            lastis += diff;

            if (write)
                PutBit(stream, book12_data[SF_DELTA + diff], length);
        }
        else if (book == HCB_PNS)
        {
            diff = coder->sf[cnt] - lastpns;

            if (initpns)
            {
                /* First PNS band has no prior energy to delta against, so the
                 * spec sends a raw 9-bit value (diff + 256) instead of a book-12
                 * code; later PNS bands delta off this one. */
                initpns = 0;

                length = 9;
                bits += length;
                lastpns += diff;

                if (write)
                    PutBit(stream, diff + 256, length);
                continue;
            }

            diff = clamp_sf_diff(diff);

            length = book12_len[SF_DELTA + diff];
            bits += length;
            lastpns += diff;

            if (write)
                PutBit(stream, book12_data[SF_DELTA + diff], length);
        }
        else if ((book != HCB_ZERO) && (book != HCB_NONE))
        {
            diff = coder->sf[cnt] - lastsf;
            diff = clamp_sf_diff(diff);
            length = book12_len[SF_DELTA + diff];

            bits += length;
            lastsf += diff;

            if (write)
                PutBit(stream, book12_data[SF_DELTA + diff], length);
        }

    }
    return bits;
}
