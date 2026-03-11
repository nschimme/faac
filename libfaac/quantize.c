/****************************************************************************
    Quantizer core functions
    quality setting, error distribution, etc.

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

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "quantize.h"
#include "huff2.h"
#include "cpu_compute.h"

static int quantize_and_count_bits(const faac_real *xr0, int gsize, int end, int start, faac_real sfacfix, faac_real *distortion, int approx_bits);

#ifdef __GNUC__
#define GCC_VERSION (__GNUC__ * 10000 \
                     + __GNUC_MINOR__ * 100 \
                     + __GNUC_PATCHLEVEL__)
#endif

typedef void (*QuantizeFunc)(const faac_real * __restrict xr, int * __restrict xi, int n, faac_real sfacfix);

#if defined(HAVE_SSE2)
extern void quantize_sse2(const faac_real * __restrict xr, int * __restrict xi, int n, faac_real sfacfix);
#endif

static void quantize_scalar(const faac_real * __restrict xr, int * __restrict xi, int n, faac_real sfacfix)
{
    const faac_real magic = MAGIC_NUMBER;
    int cnt;
    for (cnt = 0; cnt < n; cnt++)
    {
        faac_real val = xr[cnt];
        faac_real tmp = FAAC_FABS(val);

        tmp *= sfacfix;
        tmp = FAAC_SQRT(tmp * FAAC_SQRT(tmp));

        int q = (int)(tmp + magic);
        xi[cnt] = (val < 0) ? -q : q;
    }
}

static QuantizeFunc qfunc = quantize_scalar;
static faac_real xi_pow_table[1024];

void QuantizeInit(void)
{
    int i;
    for (i = 0; i < 1024; i++) {
        xi_pow_table[i] = (faac_real)FAAC_POW((faac_real)i, 1.33333333f);
    }

#if defined(HAVE_SSE2)
    CPUCaps caps = get_cpu_caps();
    if (caps & CPU_CAP_SSE2)
        qfunc = quantize_sse2;
    else
#endif
        qfunc = quantize_scalar;
}
#define NOISEFLOOR 0.4

// band sound masking
static void bmask(CoderInfo * __restrict coderInfo, faac_real * __restrict xr0, faac_real * __restrict bandqual,
                  faac_real * __restrict bandenrg, int gnum, faac_real quality)
{
  int sfb, start, end, cnt;
  int *cb_offset = coderInfo->sfb_offset;
  int last;
  faac_real avgenrg;
  faac_real powm = 0.4;
  faac_real totenrg = 0.0;
  int gsize = coderInfo->groups.len[gnum];
  const faac_real *xr;
  int win;
  int enrgcnt = 0;
  int total_len = coderInfo->sfb_offset[coderInfo->sfbn];

  for (win = 0; win < gsize; win++)
  {
      xr = xr0 + win * BLOCK_LEN_SHORT;
      for (cnt = 0; cnt < total_len; cnt++)
      {
          totenrg += xr[cnt] * xr[cnt];
      }
  }
  enrgcnt = gsize * total_len;

  if (totenrg < ((NOISEFLOOR * NOISEFLOOR) * (faac_real)enrgcnt))
  {
      for (sfb = 0; sfb < coderInfo->sfbn; sfb++)
      {
          bandqual[sfb] = 0.0;
          bandenrg[sfb] = 0.0;
      }

      return;
  }

  for (sfb = 0; sfb < coderInfo->sfbn; sfb++)
  {
    faac_real avge, maxe;
    faac_real target;

    start = cb_offset[sfb];
    end = cb_offset[sfb + 1];

    avge = 0.0;
    maxe = 0.0;
    for (win = 0; win < gsize; win++)
    {
        xr = xr0 + win * BLOCK_LEN_SHORT + start;
        int n = end - start;
        for (cnt = 0; cnt < n; cnt++)
        {
            faac_real val = xr[cnt];
            faac_real e = val * val;
            avge += e;
            if (maxe < e)
                maxe = e;
        }
    }
    bandenrg[sfb] = avge;
    maxe *= gsize;

#define NOISETONE 0.2
    if (coderInfo->block_type == ONLY_SHORT_WINDOW)
    {
        last = BLOCK_LEN_SHORT;
        avgenrg = totenrg / last;
        avgenrg *= end - start;

        target = NOISETONE * FAAC_POW(avge/avgenrg, powm);
        target += (1.0 - NOISETONE) * 0.45 * FAAC_POW(maxe/avgenrg, powm);

        target *= 1.5;
    }
    else
    {
        last = BLOCK_LEN_LONG;
        avgenrg = totenrg / last;
        avgenrg *= end - start;

        target = NOISETONE * FAAC_POW(avge/avgenrg, powm);
        target += (1.0 - NOISETONE) * 0.45 * FAAC_POW(maxe/avgenrg, powm);
    }

    target *= 10.0 / (1.0 + ((faac_real)(start+end)/last));

    bandqual[sfb] = target * quality;
  }
}

static void compute_band_bit_targets(CoderInfo *coderInfo, faac_real *bandqual, faac_real *bandenrg, int frame_target_bits, int *band_bits)
{
    int b;
    faac_real total_difficulty = 0.0;
    faac_real difficulty[MAX_SCFAC_BANDS];
    int sfbn = coderInfo->sfbn;

    if (frame_target_bits <= 0) {
        for (b = 0; b < sfbn; b++) band_bits[b] = 0;
        return;
    }

    for (b = 0; b < sfbn; b++) {
        faac_real mask = bandqual[b] * coderInfo->adj_thr[b];
        if (mask < 1e-9) mask = 1e-9;
        difficulty[b] = bandenrg[b] / mask;
        total_difficulty += difficulty[b];
    }

    if (total_difficulty <= 0) {
        for (b = 0; b < sfbn; b++) band_bits[b] = frame_target_bits / sfbn;
        return;
    }

    for (b = 0; b < sfbn; b++) {
        band_bits[b] = (int)(frame_target_bits * difficulty[b] / total_difficulty);
        if (band_bits[b] < 4) band_bits[b] = 4;
        if (band_bits[b] > 1024) band_bits[b] = 1024;
    }
}

static faac_real rd_cost(faac_real distortion, int bits, faac_real lambda)
{
    return distortion + lambda * bits;
}

enum {MAXSHORTBAND = 36};
// use band quality levels to quantize a group of windows
static void qlevel(CoderInfo * __restrict coderInfo,
                   const faac_real * __restrict xr0,
                   const faac_real * __restrict bandqual,
                   const faac_real * __restrict bandenrg,
                   int gnum,
                   int pnslevel,
                   int *band_bits,
                   faac_real *prev_scf,
                   faac_real *prev_band_energy,
                   int bitRate
                  )
{
    int sb;
#if !defined(__clang__) && defined(__GNUC__) && (GCC_VERSION >= 40600)
    /* 2^0.25 (1.50515 dB) step from AAC specs */
    static const faac_real sfstep = 1.0 / FAAC_LOG10(FAAC_SQRT(FAAC_SQRT(2.0)));
#else
    static const faac_real sfstep = 20 / 1.50515;
#endif
    int gsize = coderInfo->groups.len[gnum];
#ifndef DRM
    faac_real pnsthr = 0.1 * pnslevel;
#endif
    faac_real bitrate_index = (faac_real)bitRate / 16000.0;
    faac_real lambda = 0.02 * FAAC_POW(2.0, bitrate_index / 2.0);

    for (sb = 0; sb < coderInfo->sfbn; sb++)
    {
      faac_real sfacfix;
      int sfac;
      faac_real rmsx;
      faac_real etot;
      int xitab[FRAME_LEN + 32];
      int *xi;
      memset(xitab, 0, sizeof(xitab));
      int start, end;
      const faac_real *xr;
      int win;
      faac_real quality = bandqual[sb] * coderInfo->adj_thr[coderInfo->bandcnt];

      if (coderInfo->book[coderInfo->bandcnt] != HCB_NONE)
      {
          coderInfo->bandcnt++;
          continue;
      }

      start = coderInfo->sfb_offset[sb];
      end = coderInfo->sfb_offset[sb+1];

      etot = bandenrg[sb] / (faac_real)gsize;
      if (end <= start) {
          coderInfo->book[coderInfo->bandcnt++] = HCB_ZERO;
          continue;
      }
      rmsx = FAAC_SQRT(etot / (end - start));

      if ((rmsx < NOISEFLOOR) || (!quality))
      {
          coderInfo->book[coderInfo->bandcnt++] = HCB_ZERO;
          continue;
      }

#ifndef DRM
      if (quality < pnsthr)
      {
          coderInfo->book[coderInfo->bandcnt] = HCB_PNS;
          coderInfo->sf[coderInfo->bandcnt] +=
              FAAC_LRINT(FAAC_LOG10(etot) * (0.5 * sfstep));
          coderInfo->bandcnt++;
          continue;
      }
#endif

      sfac = FAAC_LRINT(FAAC_LOG10(quality / rmsx) * sfstep);

      /* Quantization Reuse logic */
      if (prev_scf[coderInfo->bandcnt] >= 0) {
          faac_real scf_diff = FAAC_FABS(prev_scf[coderInfo->bandcnt] - (SF_OFFSET - sfac));
          faac_real energy_diff = FAAC_FABS(prev_band_energy[coderInfo->bandcnt] - bandenrg[sb]) / (bandenrg[sb] + 1e-9);
          /* energy check: band energy change < 10% */
          if (scf_diff < 0.1 && energy_diff < 0.1) {
              /* Skip RD search and use previous sfac */
              sfacfix = ((SF_OFFSET - sfac) < 10) ? 0.0f : FAAC_POW(10, sfac / sfstep);
              goto skip_rd;
          }
      }

      /* RD search: try sfac and sfac + 1 */
      {
          faac_real d0, d1, c0, c1;
          int b0, b1;
          faac_real f0, f1;

          f0 = ((SF_OFFSET - sfac) < 10) ? 0.0f : FAAC_POW(10, sfac / sfstep);
          b0 = quantize_and_count_bits(xr0, gsize, end - start, start, f0, &d0, 1);
          c0 = rd_cost(d0, b0, lambda);

          f1 = ((SF_OFFSET - (sfac + 1)) < 10) ? 0.0f : FAAC_POW(10, (sfac + 1) / sfstep);
          b1 = quantize_and_count_bits(xr0, gsize, end - start, start, f1, &d1, 1);
          c1 = rd_cost(d1, b1, lambda);

          if (c1 < c0) {
              sfac++;
              sfacfix = f1;
          } else {
              sfacfix = f0;
          }
      }

skip_rd:
      end -= start;
      xi = xitab;
      if (sfacfix <= 0.0)
      {
          memset(xi, 0, gsize * end * sizeof(int));
      }
      else
      {
          for (win = 0; win < gsize; win++)
          {
              xr = xr0 + win * BLOCK_LEN_SHORT + start;
              qfunc(xr, xi, end, sfacfix);
              xi += end;
          }
      }
      huffbook(coderInfo, xitab, gsize * end);
      prev_scf[coderInfo->bandcnt] = (faac_real)(SF_OFFSET - sfac);
      prev_band_energy[coderInfo->bandcnt] = bandenrg[sb];
      coderInfo->sf[coderInfo->bandcnt++] += SF_OFFSET - sfac;
    }
}

static faac_real calculate_distortion(const faac_real *xr, const int *xi, int n, faac_real sfacfix)
{
    faac_real distortion = 0;
    int i;
    faac_real inv_sfacfix = (sfacfix > 0) ? 1.0f / sfacfix : 0;

    for (i = 0; i < n; i++) {
        faac_real q_val;
        faac_real diff;

        if (xi[i] == 0) {
            q_val = 0;
        } else {
            faac_real abs_xi = (faac_real)abs(xi[i]);
            q_val = abs_xi * FAAC_SQRT(abs_xi);
            q_val *= inv_sfacfix;
        }

        diff = FAAC_FABS(xr[i]) - q_val;
        distortion += diff * diff;
    }
    return distortion;
}

static int quantize_and_count_bits(const faac_real *xr0, int gsize, int end, int start, faac_real sfacfix, faac_real *distortion, int approx_bits)
{
    int xi[FRAME_LEN + 32];
    int *p_xi = xi;
    memset(xi, 0, sizeof(xi));
    int win;
    int total_len = gsize * end;
    int maxq = 0;
    int bookmin, lenmin;

    if (sfacfix <= 0.0) {
        memset(xi, 0, total_len * sizeof(int));
        *distortion = 0;
        for (win = 0; win < gsize; win++) {
            const faac_real *xr = xr0 + win * BLOCK_LEN_SHORT + start;
            int i;
            for (i = 0; i < end; i++) *distortion += xr[i] * xr[i];
        }
        return 0;
    }

    *distortion = 0;
    for (win = 0; win < gsize; win++) {
        const faac_real *xr = xr0 + win * BLOCK_LEN_SHORT + start;
        qfunc(xr, p_xi, end, sfacfix);
        *distortion += calculate_distortion(xr, p_xi, end, sfacfix);
        p_xi += end;
    }

    if (approx_bits) {
        faac_real bits = 0;
        for (win = 0; win < total_len; win++) {
            int q = abs(xi[win]);
            if (q > 0) bits += (faac_real)FAAC_LOG10(q + 1) / FAAC_LOG10(2.0);
        }
        /* add some overhead for scalefactor and side info */
        return (int)(bits + 4);
    }

    for (win = 0; win < total_len; win++) {
        int q = abs(xi[win]);
        if (maxq < q) maxq = q;
    }

    if (maxq < 1) return 0;

    if (maxq < 2) bookmin = 1;
    else if (maxq < 3) bookmin = 3;
    else if (maxq < 5) bookmin = 5;
    else if (maxq < 8) bookmin = 7;
    else if (maxq < 13) bookmin = 9;
    else bookmin = 11;

    if (bookmin < 11) {
        lenmin = huff_count_bits(xi, total_len, bookmin);
        int len2 = huff_count_bits(xi, total_len, bookmin + 1);
        if (len2 < lenmin) lenmin = len2;
    } else {
        lenmin = huff_count_bits(xi, total_len, 11);
    }

    return lenmin;
}

int BlocQuant(CoderInfo * __restrict coder, faac_real * __restrict xr, AACQuantCfg *aacquantCfg, int frame_target_bits, faac_real *prev_scf, faac_real *prev_band_energy)
{
    faac_real bandlvl[MAX_SCFAC_BANDS];
    faac_real bandenrg[MAX_SCFAC_BANDS];
    int band_bits[MAX_SCFAC_BANDS];
    int cnt;
    faac_real *gxr;

    coder->global_gain = 0;

    coder->bandcnt = 0;
    coder->datacnt = 0;
#ifdef DRM
    coder->iLenReordSpData = 0; /* init length of reordered spectral data */
    coder->iLenLongestCW = 0; /* init length of longest codeword */
    coder->cur_cw = 0; /* init codeword counter */
#endif

    {
        int lastis;
        int lastsf;

        gxr = xr;
        for (cnt = 0; cnt < coder->groups.n; cnt++)
        {
            bmask(coder, gxr, bandlvl, bandenrg, cnt,
                  (faac_real)aacquantCfg->quality/DEFQUAL);

            compute_band_bit_targets(coder, bandlvl, bandenrg, frame_target_bits, band_bits);

            qlevel(coder, gxr, bandlvl, bandenrg, cnt, aacquantCfg->pnslevel, band_bits, prev_scf, prev_band_energy, aacquantCfg->bitRate);
            gxr += coder->groups.len[cnt] * BLOCK_LEN_SHORT;
        }

        coder->global_gain = 0;
        for (cnt = 0; cnt < coder->bandcnt; cnt++)
        {
            int book = coder->book[cnt];
            if (!book)
                continue;
            if ((book != HCB_INTENSITY) && (book != HCB_INTENSITY2))
            {
                coder->global_gain = coder->sf[cnt];
                break;
            }
        }

        lastsf = coder->global_gain;
        lastis = 0;
        // fixme: move SF range check to quantizer
        for (cnt = 0; cnt < coder->bandcnt; cnt++)
        {
            int book = coder->book[cnt];
            if ((book == HCB_INTENSITY) || (book == HCB_INTENSITY2))
            {
                int diff = coder->sf[cnt] - lastis;

                if (diff < -60)
                    diff = -60;
                if (diff > 60)
                    diff = 60;

                lastis += diff;
                coder->sf[cnt] = lastis;
            }
            else if (book == HCB_ESC)
            {
                int diff = coder->sf[cnt] - lastsf;

                if (diff < -60)
                    diff = -60;
                if (diff > 60)
                    diff = 60;

                lastsf += diff;
                coder->sf[cnt] = lastsf;
            }
        }
        return 1;
    }
    return 0;
}

void CalcBW(unsigned *bw, int rate, SR_INFO *sr, AACQuantCfg *aacquantCfg)
{
    // find max short frame band
    int max = *bw * (BLOCK_LEN_SHORT << 1) / rate;
    int cnt;
    int l;

    l = 0;
    for (cnt = 0; cnt < sr->num_cb_short; cnt++)
    {
        if (l >= max)
            break;
        l += sr->cb_width_short[cnt];
    }
    aacquantCfg->max_cbs = cnt;
    if (aacquantCfg->pnslevel)
        *bw = (faac_real)l * rate / (BLOCK_LEN_SHORT << 1);

    // find max long frame band
    max = *bw * (BLOCK_LEN_LONG << 1) / rate;
    l = 0;
    for (cnt = 0; cnt < sr->num_cb_long; cnt++)
    {
        if (l >= max)
            break;
        l += sr->cb_width_long[cnt];
    }
    aacquantCfg->max_cbl = cnt;
    aacquantCfg->max_l = l;

    *bw = (faac_real)l * rate / (BLOCK_LEN_LONG << 1);
}

enum {MINSFB = 2};

static void calce(faac_real * __restrict xr, const int * __restrict bands, faac_real e[NSFB_SHORT], int maxsfb,
                  int maxl)
{
    int sfb;
    int l;

    // mute lines above cutoff freq
    for (l = maxl; l < bands[maxsfb]; l++)
        xr[l] = 0.0;

    for (sfb = MINSFB; sfb < maxsfb; sfb++)
    {
        e[sfb] = 0;
        for (l = bands[sfb]; l < bands[sfb + 1]; l++)
            e[sfb] += xr[l] * xr[l];
    }
}

static void resete(faac_real min[NSFB_SHORT], faac_real max[NSFB_SHORT],
                   faac_real e[NSFB_SHORT], int maxsfb)
{
    int sfb;
    for (sfb = MINSFB; sfb < maxsfb; sfb++)
        min[sfb] = max[sfb] = e[sfb];
}

#define PRINTSTAT 0
#if PRINTSTAT
static int groups = 0;
static int frames = 0;
#endif
void BlocGroup(faac_real *xr, CoderInfo *coderInfo, AACQuantCfg *cfg)
{
    int win, sfb;
    faac_real e[NSFB_SHORT];
    faac_real min[NSFB_SHORT];
    faac_real max[NSFB_SHORT];
    const faac_real thr = 3.0;
    int win0;
    int fastmin;
    int maxsfb, maxl;

    if (coderInfo->block_type != ONLY_SHORT_WINDOW)
    {
        coderInfo->groups.n = 1;
        coderInfo->groups.len[0] = 1;
        return;
    }

    maxl = cfg->max_l / 8;
    maxsfb = cfg->max_cbs;
    fastmin = ((maxsfb - MINSFB) * 3) >> 2;

#ifdef DRM
    coderInfo->groups.n = 1;
    coderInfo->groups.len[0] = 8;
    return;
#endif

#if PRINTSTAT
    frames++;
#endif
    calce(xr, coderInfo->sfb_offset, e, maxsfb, maxl);
    resete(min, max, e, maxsfb);
    win0 = 0;
    coderInfo->groups.n = 0;
    for (win = 1; win < MAX_SHORT_WINDOWS; win++)
    {
        int fast = 0;

        calce(xr + win * BLOCK_LEN_SHORT, coderInfo->sfb_offset, e, maxsfb, maxl);
        for (sfb = MINSFB; sfb < maxsfb; sfb++)
        {
            if (min[sfb] > e[sfb])
                min[sfb] = e[sfb];
            if (max[sfb] < e[sfb])
                max[sfb] = e[sfb];

            if (max[sfb] > thr * min[sfb])
                fast++;
        }
        if (fast > fastmin)
        {
            coderInfo->groups.len[coderInfo->groups.n++] = win - win0;
            win0 = win;
            resete(min, max, e, maxsfb);
        }
    }
    coderInfo->groups.len[coderInfo->groups.n++] = win - win0;
#if PRINTSTAT
    groups += coderInfo->groups.n;
#endif
}

void BlocStat(void)
{
#if PRINTSTAT
    printf("frames:%d; groups:%d; g/f:%f\n", frames, groups, (faac_real)groups/frames);
#endif
}
