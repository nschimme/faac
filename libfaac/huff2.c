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
#include "util.h"

/* Escape suffix for HCB_ESC: a magnitude |q| >= LAV_ESC is sent as the pair
 * index LAV_ESC (emitted by the caller) plus this suffix - a unary prefix of
 * `preflen` ones and a zero, then the low `preflen+4` bits of x.
 * preflen counts how far x is past the 16-window, so the suffix is 2*preflen+5
 * bits. */
static int escape(int x, int *code)
{
    int preflen;
    int base;

    if (x > MAX_HUFF_ESC_VAL)
    {
        fprintf(stderr, "%s(%d): x_quant > %d\n", __FILE__, __LINE__, MAX_HUFF_ESC_VAL);
        return 0;
    }

    preflen = 31 - CountLeadingZeros(x) - 4;
    base = 1 << (preflen + 4);

    *code = (1 << (preflen + 1)) - 2; /* preflen 1s and a 0 */
    *code <<= (preflen + 4);
    *code |= (x - base);

    return (preflen << 1) + 5;
}

/* Drive a per-tuple BODY over [qs, len). Full stride-aligned tuples are read
 * directly from qs via a pointer (no copy, no per-iteration bounds branch) — this
 * is the hot path. A trailing partial tuple (len not a stride multiple) is
 * zero-padded once. Spectral SFBs are stride-aligned in practice, so the tail is a
 * safety net for misalignment that does not run in the inner loop. */
#define FOR_TUPLES(stride, BODY) \
    do { \
        int _ofs, _full = len - (len % (stride)); \
        for (_ofs = 0; _ofs < _full; _ofs += (stride)) \
            BODY(qs + _ofs); \
        if (_ofs < len) { \
            int _pad[stride] = {0}, _j; \
            for (_j = 0; _j < len - _ofs; _j++) _pad[_j] = qs[_ofs + _j]; \
            BODY(_pad); \
        } \
    } while (0)

/* Tuple indexing: maps signed or magnitude tuples into the polynomial space
 * of the Huffman LUTs. Signed books fold sign into the index (biased by 40)
 * to eliminate explicit sign bits for low-energy coefficients. */
#define IDX_S4(q) (DIM_S4*DIM_S4*DIM_S4*(q)[0] + DIM_S4*DIM_S4*(q)[1] + DIM_S4*(q)[2] + (q)[3] + 40)
#define IDX_S2(q) (DIM_S2*(q)[0] + (q)[1] + 40)
#define IDX_M4(q) (DIM_M4*DIM_M4*DIM_M4*abs((q)[0]) + DIM_M4*DIM_M4*abs((q)[1]) + DIM_M4*abs((q)[2]) + abs((q)[3]))
#define IDX_M2(q, b) ((b)*abs((q)[0]) + abs((q)[1]))

/* huffcode() per-tuple bodies, driven by FOR_TUPLES over a tuple pointer p.
 * coder == NULL sizes only; else also emits the codeword to coder->s[]. blens,
 * bdata, coder, datacnt and bits are the enclosing huffcode() locals.
 * Signed books fold sign into the index, so they emit bdata[idx] verbatim;
 * magnitude books look up the |coef| index then append one sign bit per nonzero
 * coefficient to both the length and (when emitting) the codeword. */
#define EMIT_LEN(d, l) \
    do { if (coder) { coder->s[datacnt].data = (d); coder->s[datacnt++].len = (l); } \
         bits += (l); } while (0)

#define HC_SIGNED(p, idx_expr) \
    do { int _i = (idx_expr); EMIT_LEN(bdata[_i], blens[_i]); } while (0)

#define HC_MAG(p, n, idx_expr) \
    do { \
        if (!mag_nonzero_##n(p)) { EMIT_LEN(bdata[0], blens[0]); } \
        else { \
            int _i = (idx_expr), _l = blens[_i], _d = coder ? bdata[_i] : 0; \
            for (int _j = 0; _j < (n); _j++) if ((p)[_j]) \
                { _l++; if (coder) { _d <<= 1; if ((p)[_j] < 0) _d |= 1; } } \
            EMIT_LEN(_d, _l); \
        } \
    } while (0)

#define mag_nonzero_4(p) ((p)[0] | (p)[1] | (p)[2] | (p)[3])
#define mag_nonzero_2(p) ((p)[0] | (p)[1])

#define HC_S4(p)  HC_SIGNED(p, IDX_S4(p))
#define HC_S2(p)  HC_SIGNED(p, IDX_S2(p))
#define HC_M4(p)  HC_MAG(p, 4, IDX_M4(p))
#define HC_M2_7(p)  HC_MAG(p, 2, IDX_M2(p, DIM_M2_7))
#define HC_M2_12(p) HC_MAG(p, 2, IDX_M2(p, DIM_M2_12))

/* ESC book (11): magnitude 2-tuple clamped to LAV_ESC, with an escape-coded
 * suffix carrying the full magnitude for any coefficient at the clamp. */
#define HC_ESC(p) \
    do { \
        if (!mag_nonzero_2(p)) { EMIT_LEN(bdata[0], blens[0]); } \
        else { \
            int _a0 = abs((p)[0]), _a1 = abs((p)[1]); \
            int _x0 = (_a0 > LAV_ESC) ? LAV_ESC : _a0; \
            int _x1 = (_a1 > LAV_ESC) ? LAV_ESC : _a1; \
            int _i = DIM_ESC * _x0 + _x1, _l = blens[_i], _d = coder ? bdata[_i] : 0; \
            for (int _j = 0; _j < 2; _j++) if ((p)[_j]) \
                { _l++; if (coder) { _d <<= 1; if ((p)[_j] < 0) _d |= 1; } } \
            EMIT_LEN(_d, _l); \
            if (_a0 >= LAV_ESC) { int _ed = 0, _eb = escape(_a0, &_ed); EMIT_LEN(_ed, _eb); } \
            if (_a1 >= LAV_ESC) { int _ed = 0, _eb = escape(_a1, &_ed); EMIT_LEN(_ed, _eb); } \
        } \
    } while (0)

/* Code `len` coeffs under book `bnum`, returning bit count. coder == NULL only
 * sizes (the hot cost path); else also emits codewords to coder->s[]. len/code
 * tables are split (huff_len/huff_data, sliced by huff_offset) so the cost path
 * streams uint8 lengths only to minimize cache pressure. */
static int huffcode(int *qs /* quantized spectrum */,
                    int len,
                    int bnum,
                    CoderInfo *coder)
{
    const uint8_t *blens = huff_len + huff_offset[bnum - 1];
    const uint16_t *bdata = huff_data + huff_offset[bnum - 1];
    int bits = 0, datacnt = coder ? coder->datacnt : 0;

    switch (bnum)
    {
    case HCB_1:
    case HCB_2:  FOR_TUPLES(4, HC_S4);    break;
    case HCB_3:
    case HCB_4:  FOR_TUPLES(4, HC_M4);    break;
    case HCB_5:
    case HCB_6:  FOR_TUPLES(2, HC_S2);    break;
    case HCB_7:
    case HCB_8:  FOR_TUPLES(2, HC_M2_7);  break;
    case HCB_9:
    case HCB_10: FOR_TUPLES(2, HC_M2_12); break;
    case HCB_ESC:
        FOR_TUPLES(2, HC_ESC);
        break;
    default:
        fprintf(stderr, "%s(%d) book %d out of range\n", __FILE__, __LINE__, bnum);
        return -1;
    }

    if (coder) coder->datacnt = datacnt;
    return bits;
}

/* Lowest base codebook whose range-pair covers |maxq| (keeps the decoder in
 * range, cf. the escape/out-of-range guards). Returns the lower book of the
 * pair; HCB_ZERO for an empty band, HCB_ESC for the escape book. Shared by
 * huffbook() (per-band selection) and section_optimize() (merge re-costing). */
static int book_for_maxq(int maxq)
{
    if (maxq < 1) return HCB_ZERO;
    if (maxq < LAV_1 + 1) return HCB_1;
    if (maxq < LAV_2 + 1) return HCB_3;
    if (maxq < LAV_4 + 1) return HCB_5;
    if (maxq < LAV_7 + 1) return HCB_7;
    if (maxq < LAV_12 + 1) return HCB_9;
    return HCB_ESC;
}

/* Bit cost of coding [qs,len) under BOTH books of a range-pair in one traversal.
 * `base` is the lower book (HCB_1, HCB_3, HCB_5, HCB_7, HCB_9 — never ESC).
 * The two books of a pair share the same fetch and polynomial space mapping;
 * we fuse both lookups into a single pass to accelerate the greedy merge search.
 * Results match two huffcode(..., 0) calls exactly. */
/* huff_count_pair() per-tuple bodies: accumulate the length of each tuple under
 * both books of the pair (la == base, lb == base+1). la/lb/c0/c1 are the
 * enclosing locals; sign bits add equally to both books. */
#define CP_S4(p) do { int _i = IDX_S4(p); c0 += la[_i]; c1 += lb[_i]; } while (0)
#define CP_S2(p) do { int _i = IDX_S2(p); c0 += la[_i]; c1 += lb[_i]; } while (0)

#define CP_MAG(p, n, idx_expr) \
    do { \
        if (!mag_nonzero_##n(p)) { c0 += la[0]; c1 += lb[0]; } \
        else { \
            int _i = (idx_expr), _s = 0; \
            for (int _j = 0; _j < (n); _j++) if ((p)[_j]) _s++; \
            c0 += la[_i] + _s; c1 += lb[_i] + _s; \
        } \
    } while (0)

#define CP_M4(p)    CP_MAG(p, 4, IDX_M4(p))
#define CP_M2_7(p)  CP_MAG(p, 2, IDX_M2(p, DIM_M2_7))
#define CP_M2_12(p) CP_MAG(p, 2, IDX_M2(p, DIM_M2_12))

static void huff_count_pair(const int *qs, int len, int base, int *pc0, int *pc1)
{
    const uint8_t *la = huff_len + huff_offset[base - 1];
    const uint8_t *lb = huff_len + huff_offset[base];
    int c0 = 0, c1 = 0;

    switch (base)
    {
    case HCB_1:  FOR_TUPLES(4, CP_S4);    break;  /* HCB_1, HCB_2: signed 4-tuple */
    case HCB_3:  FOR_TUPLES(4, CP_M4);    break;  /* HCB_3, HCB_4: magnitude 4-tuple */
    case HCB_5:  FOR_TUPLES(2, CP_S2);    break;  /* HCB_5, HCB_6: signed 2-tuple */
    case HCB_7:  FOR_TUPLES(2, CP_M2_7);  break;  /* HCB_7, HCB_8: magnitude 2-tuple */
    default:     FOR_TUPLES(2, CP_M2_12); break;  /* HCB_9, HCB_10: magnitude 2-tuple */
    }
    *pc0 = c0;
    *pc1 = c1;
}

/* Select the cheapest codebook for one spectral band; symbols are NOT emitted here
 * (deferred to emit_spectral after section_optimize has finalised the books). The
 * band's own-tier range-pair cost is cached in cost_pair[] for the DP to sum
 * same-tier spans for free; cross-tier spans it recosts on demand. Called by
 * huffman_finalize() for every band still HCB_NONE (qlevel resolves zero/PNS/IS). */
static void huffbook(CoderInfo *coder, int band)
{
    int *qs = coder->qspec + coder->qoffset[band];
    int len = coder->qlen[band];
    int *cp = coder->cost_pair[band];
    int maxq = 0, base, i;

    for (i = 0; i < len; i++)
    {
        int aq = abs(qs[i]);
        if (aq > maxq) maxq = aq;
    }

    base = book_for_maxq(maxq);
    if (base == HCB_ZERO)
    {
        coder->book[band] = HCB_ZERO;
        cp[0] = cp[1] = 0;
    }
    else if (base == HCB_ESC)
    {
        int c = huffcode(qs, len, HCB_ESC, 0);
        coder->book[band] = HCB_ESC;
        cp[0] = cp[1] = c;   /* ESC tier has no pair partner */
    }
    else
    {
        /* range-pair: keep whichever of {base, base+1} codes the band shorter */
        int c0, c1;
        huff_count_pair(qs, len, base, &c0, &c1);
        cp[0] = c0;
        cp[1] = c1;
        coder->book[band] = (c1 < c0) ? base + 1 : base;
    }
}

/* Range tier of a spectral book (higher tier covers larger |coef|); 0 for
 * HCB_ZERO and non-spectral books. */
static int book_tier(int book)
{
    switch (book)
    {
    case HCB_1:
    case HCB_2:  return 1;
    case HCB_3:
    case HCB_4:  return 2;
    case HCB_5:
    case HCB_6:  return 3;
    case HCB_7:
    case HCB_8:  return 4;
    case HCB_9:
    case HCB_10: return 5;
    case HCB_ESC:    return 6;
    default:         return 0;
    }
}

static int tier_base_book(int tier)
{
    switch (tier)
    {
    case 1: return HCB_1;
    case 2: return HCB_3;
    case 3: return HCB_5;
    case 4: return HCB_7;
    case 5: return HCB_9;
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

/* Optimal section formation by dynamic programming. A section is a maximal run of
 * bands sharing one codebook; the cost traded off is spectral bits vs one section
 * header per book change. Optimal never splits inside a same-tier run (that pays a
 * header for zero spectral change), so the only cut points are tier boundaries:
 * collapse each spectral run into same-tier SEGMENTS, then DP over the S segments
 * instead of the N bands.
 *
 *   best[k] = min over segment-start s of best[s] + header + min_covering_bits(s,k)
 *
 * To keep the O(S^2) inner loop off huff_count_pair, hoist all spectral re-walking
 * into a precompute: seg[i][T] = segment i's bit cost under every present covering
 * tier T >= its own (own tier is the cached cost_pair sum, free; higher tiers are
 * the only recosts). The DP then costs any section by integer-summing seg[i][T]
 * over a small L1-hot table. Lossless: only reassigns spectral books, never
 * scalefactors or band class. */
static void section_optimize(CoderInfo *coder)
{
    int shortwin = (coder->block_type == ONLY_SHORT_WINDOW);
    int maxcnt = shortwin ? 7 : 31;
    int header = 4 + (shortwin ? 3 : 5);
    int g;

    for (g = 0; g < coder->groups.n; g++)
    {
        int b0 = g * coder->sfbn;
        int bN = b0 + coder->sfbn;
        int b = b0;

        while (b < bN)
        {
            int e, S, i, k, T;
            int sg_band[MAX_SCFAC_BANDS], sg_nb[MAX_SCFAC_BANDS], sg_tier[MAX_SCFAC_BANDS];
            int sg_ofs[MAX_SCFAC_BANDS], sg_len[MAX_SCFAC_BANDS];
            int seg0[MAX_SCFAC_BANDS][7], seg1[MAX_SCFAC_BANDS][7];  /* cost under tier T */
            int present[7];
            int best[MAX_SCFAC_BANDS + 1], split[MAX_SCFAC_BANDS + 1], bch[MAX_SCFAC_BANDS + 1];

            if (!is_spectral(coder->book[b])) { b++; continue; }

            for (e = b; e < bN && is_spectral(coder->book[e]); e++)
                ;

            /* collapse [b,e) into same-tier segments; own-tier cost from cost_pair */
            for (T = 0; T < 7; T++) present[T] = 0;
            S = 0;
            for (i = b; i < e; )
            {
                int t = book_tier(coder->book[i]);
                int c0 = 0, c1 = 0, len = 0, j = i;
                while (j < e && book_tier(coder->book[j]) == t)
                {
                    c0 += coder->cost_pair[j][0];
                    c1 += coder->cost_pair[j][1];
                    len += coder->qlen[j];
                    j++;
                }
                sg_band[S] = i; sg_nb[S] = j - i; sg_tier[S] = t;
                sg_ofs[S] = coder->qoffset[i]; sg_len[S] = len;
                seg0[S][t] = c0; seg1[S][t] = (t < 6) ? c1 : COST_INF;
                present[t] = 1;
                S++;
                i = j;
            }

            /* precompute each segment's cost under every present covering tier > own */
            for (i = 0; i < S; i++)
            {
                for (T = sg_tier[i] + 1; T < 7; T++)
                {
                    if (!present[T]) continue;
                    if (T < 6)
                        huff_count_pair(coder->qspec + sg_ofs[i], sg_len[i],
                                        tier_base_book(T), &seg0[i][T], &seg1[i][T]);
                    else
                    {
                        seg0[i][6] = huffcode(coder->qspec + sg_ofs[i], sg_len[i], HCB_ESC, 0);
                        seg1[i][6] = COST_INF;
                    }
                }
            }

            /* DP: inner step is integer adds over seg[][]; re-sum only on a tier rise */
            best[0] = 0;
            for (k = 1; k <= S; k++)
            {
                int cur = sg_tier[k - 1], nb = sg_nb[k - 1], s;
                int c0 = seg0[k - 1][cur], c1 = seg1[k - 1][cur];

                best[k] = COST_INF;
                for (s = k - 1; s >= 0; s--)
                {
                    int base, sc, sbook, tot;

                    if (s < k - 1)
                    {
                        int t = sg_tier[s];

                        nb += sg_nb[s];
                        if (nb > maxcnt)
                            break;

                        if (t > cur)
                        {
                            int m;                  /* covering rises: re-sum [s,k) */
                            cur = t;
                            c0 = 0; c1 = (cur < 6) ? 0 : COST_INF;
                            for (m = s; m < k; m++)
                            {
                                c0 += seg0[m][cur];
                                if (cur < 6) c1 += seg1[m][cur];
                            }
                        }
                        else
                        {
                            c0 += seg0[s][cur];
                            c1 = (cur < 6) ? c1 + seg1[s][cur] : COST_INF;
                        }
                    }

                    base = tier_base_book(cur);
                    if (c1 < c0) { sc = c1; sbook = base + 1; }
                    else         { sc = c0; sbook = base; }

                    tot = best[s] + header + sc;
                    if (tot < best[k]) { best[k] = tot; split[k] = s; bch[k] = sbook; }
                }
            }

            for (k = S; k > 0; )
            {
                int s = split[k], from = sg_band[s];
                int to = sg_band[k - 1] + sg_nb[k - 1], m;
                for (m = from; m < to; m++)
                    coder->book[m] = bch[k];
                k = s;
            }
            b = e;
        }
    }
}

/* Emit Huffman symbols for all spectral bands in band order, filling coder->s[]
 * for WriteSpectralData(). Adjacent bands with the same book are combined into
 * a single huffcode call to maximize throughput. */
static void emit_spectral(CoderInfo *coder)
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

/* With books final, seed global_gain and reconstruct the intensity/PNS
 * scalefactor delta chains so coder->sf[] holds exactly what writesf() will
 * encode (the regular-band chain is pre-clamped in qlevel). global_gain is an
 * 8-bit field that seeds the decoder's regular chain, so it must come from a
 * regular band's sf, never an intensity/PNS band (different scale, can be
 * negative) which would truncate on write and desync the chains. */
static void finalize_sf_chain(CoderInfo *coder)
{
    int cnt, lastis = 0, lastpns;

    coder->global_gain = 0;
    for (cnt = 0; cnt < coder->bandcnt; cnt++)
    {
        int book = coder->book[cnt];
        if (!book)
            continue;
        if ((book != HCB_INTENSITY) && (book != HCB_INTENSITY2) && (book != HCB_PNS))
        {
            coder->global_gain = coder->sf[cnt];
            break;
        }
    }

    lastpns = coder->global_gain - SF_PNS_OFFSET;
    for (cnt = 0; cnt < coder->bandcnt; cnt++)
    {
        int book = coder->book[cnt];
        if ((book == HCB_INTENSITY) || (book == HCB_INTENSITY2))
        {
            lastis += clamp_sf_diff(coder->sf[cnt] - lastis);
            coder->sf[cnt] = lastis;
        }
        else if (book == HCB_PNS)
        {
            lastpns += clamp_sf_diff(coder->sf[cnt] - lastpns);
            coder->sf[cnt] = lastpns;
        }
    }
}

void huffman_finalize(CoderInfo *coder)
{
    int b;

    /* qlevel() leaves every spectral, non-empty band marked HCB_NONE; resolve
     * each to its cheapest codebook before merging and emitting. */
    for (b = 0; b < coder->bandcnt; b++)
        if (coder->book[b] == HCB_NONE)
            huffbook(coder, b);

    section_optimize(coder);
    emit_spectral(coder);
    finalize_sf_chain(coder);
}

/* Write (or size) the section layout: 4-bit book + run length per maximal run of
 * equal adjacent books. The count field is cntbits wide (5 long / 3 short), so a
 * run past maxcnt splits into repeated full sections. greedy merge grows the
 * runs; this just RLE-encodes the final books[]. */
int writebooks(CoderInfo *coder, BitStream *stream, int write)
{
    int bits = 0;
    int maxcnt, cntbits;
    int group;
    int bookbits = 4;

    if (coder->block_type == ONLY_SHORT_WINDOW)
    {
        maxcnt = 7;
        cntbits = 3;
    }
    else
    {
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
            if (write)
            {
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
                {
                    PutBit(stream, maxcnt, cntbits);
                }
                bits += cntbits;
                bookcnt -= maxcnt;
            }
            if (write)
            {
                PutBit(stream, bookcnt, cntbits);
            }
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

    /* Three independent DPCM chains share HCB_DELTA, each seeded so its first delta
     * is self-contained: intensity positions from 0, scalefactors from global_gain
     * (itself the first regular sf), PNS energies from global_gain - SF_PNS_OFFSET.
     * The decoder rebuilds each by the same running sum, so deltas match. */
    lastsf = coder->global_gain;
    lastis = 0;
    lastpns = coder->global_gain - SF_PNS_OFFSET;

    /* qlevel() bounds every stored sf[] and global_gain to [0, SF_MAX_ABS], so
     * the running reconstruction below cannot leave the decoder's range. */
    for (cnt = 0; cnt < coder->bandcnt; cnt++)
    {
        int book = coder->book[cnt];

        if ((book == HCB_INTENSITY) || (book == HCB_INTENSITY2))
        {
            diff = coder->sf[cnt] - lastis;
            diff = clamp_sf_diff(diff);
            length = huff_len_delta[SF_DELTA + diff];

            bits += length;

            lastis += diff;

            if (write)
            {
                PutBit(stream, huff_data_delta[SF_DELTA + diff], length);
            }
        }
        else if (book == HCB_PNS)
        {
            diff = coder->sf[cnt] - lastpns;

            if (initpns)
            {
                /* First PNS band has no prior energy to delta against, so the
                 * spec sends a raw 9-bit value (diff + 256) instead of an HCB_DELTA
                 * code; later PNS bands delta off this one. */
                initpns = 0;

                length = 9;
                bits += length;
                lastpns += diff;

                if (write)
                {
                    PutBit(stream, diff + 256, length);
                }
                continue;
            }

            diff = clamp_sf_diff(diff);

            length = huff_len_delta[SF_DELTA + diff];
            bits += length;
            lastpns += diff;

            if (write)
            {
                PutBit(stream, huff_data_delta[SF_DELTA + diff], length);
            }
        }
        else if ((book != HCB_ZERO) && (book != HCB_NONE))
        {
            diff = coder->sf[cnt] - lastsf;
            diff = clamp_sf_diff(diff);
            length = huff_len_delta[SF_DELTA + diff];

            bits += length;
            lastsf += diff;

            if (write)
            {
                PutBit(stream, huff_data_delta[SF_DELTA + diff], length);
            }
        }

    }
    return bits;
}
