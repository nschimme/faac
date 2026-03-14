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

#ifdef __GNUC__
#define GCC_VERSION (__GNUC__ * 10000 \
                     + __GNUC_MINOR__ * 100 \
                     + __GNUC_PATCHLEVEL__)
#endif

typedef void (*QuantizeFunc)(const faac_real * __restrict xr, int * __restrict xi, int n, faac_real sfacfix);

#if defined(HAVE_SSE2)
extern void quantize_sse2(const faac_real * __restrict xr, int * __restrict xi, int n, faac_real sfacfix);
#endif

static faac_real pow43_lookup[8192];

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

void QuantizeInit(void)
{
    int i;
    for (i = 0; i < 8192; i++)
        pow43_lookup[i] = FAAC_POW((faac_real)i, 4.0/3.0);

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

enum {MAXSHORTBAND = 36};
// use band quality levels to quantize a group of windows
static void qlevel(CoderInfo * __restrict coderInfo,
                   const faac_real * __restrict xr0,
                   const faac_real * __restrict bandqual,
                   const faac_real * __restrict bandenrg,
                   int gnum,
                   int pnslevel,
                   AACQuantCfg *aacquantCfg
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
    faac_real pnsthr = 0.1 * pnslevel;

    for (sb = 0; sb < coderInfo->sfbn; sb++)
    {
      int sfac;
      faac_real rmsx;
      faac_real etot;
      int start, end;
      const faac_real *xr;
      int win;

      if (coderInfo->book[coderInfo->bandcnt] != HCB_NONE)
      {
          coderInfo->bandcnt++;
          continue;
      }

      start = coderInfo->sfb_offset[sb];
      end = coderInfo->sfb_offset[sb+1];

      etot = bandenrg[sb] / (faac_real)gsize;

      // Fast exit for low energy bands
      if (etot < 1e-9)
      {
          coderInfo->book[coderInfo->bandcnt++] = HCB_ZERO;
          continue;
      }

      rmsx = FAAC_SQRT(etot / (end - start));

      if ((rmsx < NOISEFLOOR) || (!bandqual[sb]))
      {
          coderInfo->book[coderInfo->bandcnt++] = HCB_ZERO;
          continue;
      }

      if (bandqual[sb] < pnsthr)
      {
          coderInfo->book[coderInfo->bandcnt] = HCB_PNS;
          coderInfo->sf[coderInfo->bandcnt] +=
              FAAC_LRINT(FAAC_LOG10(etot) * (0.5 * sfstep));
          coderInfo->bandcnt++;
          continue;
      }

      sfac = FAAC_LRINT(FAAC_LOG10(bandqual[sb] / rmsx) * sfstep);

      int width = end - start;

      int best_sf = sfac;
      faac_real best_cost = 1e36;
      int best_book = HCB_ZERO;
      int best_xi[BLOCK_LEN_LONG];
      faac_real lambda = aacquantCfg->lambda;
      int i;

      memset(best_xi, 0, sizeof(best_xi));

      for (int cand_sf = sfac - 1; cand_sf <= sfac + 1; cand_sf++)
      {
          int cand_xi[BLOCK_LEN_LONG];
          faac_real cand_sfacfix;
          faac_real cand_dist = 0;
          int cand_bits = 0;
          int cand_book = HCB_ZERO;
          int maxq = 0;

          if ((SF_OFFSET - cand_sf) < 10)
              cand_sfacfix = 0.0;
          else
              cand_sfacfix = FAAC_POW(10, cand_sf / sfstep);

          if (cand_sfacfix <= 0.0)
          {
              memset(cand_xi, 0, gsize * width * sizeof(int));
          }
          else
          {
              for (win = 0; win < gsize; win++)
              {
                  xr = xr0 + win * BLOCK_LEN_SHORT + start;
                  qfunc(xr, cand_xi + win * width, width, cand_sfacfix);
              }
          }

          // Distortion
          faac_real i_sfacfix = (cand_sfacfix > 0) ? 1.0 / cand_sfacfix : 0;
          for (win = 0; win < gsize; win++)
          {
              xr = xr0 + win * BLOCK_LEN_SHORT + start;
              const int * __restrict xi = cand_xi + win * width;
              for (i = 0; i < width; i++)
              {
                  int q_val = xi[i];
                  int q_idx = abs(q_val);
                  faac_real xr_q;
                  if (q_idx < 8192)
                      xr_q = pow43_lookup[q_idx] * i_sfacfix;
                  else
                      xr_q = FAAC_POW((faac_real)q_idx, 4.0/3.0) * i_sfacfix;

                  if (q_val < 0) xr_q = -xr_q;
                  faac_real err = xr[i] - xr_q;
                  cand_dist += err * err;
                  if (q_idx > maxq) maxq = q_idx;
              }
          }

          // Bits
          if (maxq == 0) cand_book = HCB_ZERO;
          else if (maxq < 2) cand_book = 1;
          else if (maxq < 3) cand_book = 3;
          else if (maxq < 5) cand_book = 5;
          else if (maxq < 8) cand_book = 7;
          else if (maxq < 13) cand_book = 9;
          else cand_book = HCB_ESC;

          if (cand_book != HCB_ZERO)
          {
              cand_bits = huff_count_bits(cand_xi, gsize * width, cand_book);
              if (cand_book < 11 && (cand_book & 1))
              {
                  int bnext_bits = huff_count_bits(cand_xi, gsize * width, cand_book + 1);
                  if (bnext_bits < cand_bits)
                  {
                      cand_bits = bnext_bits;
                      cand_book++;
                  }
              }
          }

          faac_real cost;
          if (cand_bits < 0 || maxq >= 8192)
              cost = 1e37;
          else {
              faac_real err_weight = 1.0 / (bandqual[sb] * bandqual[sb] * gsize * width + 1e-15);
              cost = cand_dist * err_weight + lambda * (faac_real)cand_bits;
          }

          if (cost < best_cost)
          {
              best_cost = cost;
              best_sf = cand_sf;
              best_book = cand_book;
              memcpy(best_xi, cand_xi, gsize * width * sizeof(int));
          }
      }

      if (best_book > HCB_ZERO)
          huffcode(best_xi, gsize * width, best_book, coderInfo);
      coderInfo->book[coderInfo->bandcnt] = best_book;
      coderInfo->sf[coderInfo->bandcnt++] = SF_OFFSET - best_sf;
    }
}

static void clamp_sf(CoderInfo *coder)
{
    int lastsf = coder->global_gain;
    int lastis = 0;
    int lastpns = coder->global_gain - 90;
    int cnt;

    for (cnt = 0; cnt < coder->bandcnt; cnt++)
    {
        int book = coder->book[cnt];
        if (book > 0 && book < HCB_PNS)
        {
            int diff = coder->sf[cnt] - lastsf;
            if (diff < -60) diff = -60;
            if (diff > 60) diff = 60;
            lastsf += diff;
            coder->sf[cnt] = lastsf;
        }
        else if (book == HCB_INTENSITY || book == HCB_INTENSITY2)
        {
            int diff = coder->sf[cnt] - lastis;
            if (diff < -60) diff = -60;
            if (diff > 60) diff = 60;
            lastis += diff;
            coder->sf[cnt] = lastis;
        }
        else if (book == HCB_PNS)
        {
            int diff = coder->sf[cnt] - lastpns;
            if (diff < -60) diff = -60;
            if (diff > 60) diff = 60;
            lastpns += diff;
            coder->sf[cnt] = lastpns;
        }
    }
}

int BlocQuant(CoderInfo * __restrict coder, faac_real * __restrict xr, AACQuantCfg *aacquantCfg)
{
    faac_real bandlvl[MAX_SCFAC_BANDS];
    faac_real bandenrg[MAX_SCFAC_BANDS];
    int cnt;
    faac_real *gxr;

    coder->global_gain = 0;

    coder->bandcnt = 0;
    coder->datacnt = 0;

    {
        gxr = xr;
        for (cnt = 0; cnt < coder->groups.n; cnt++)
        {
            bmask(coder, gxr, bandlvl, bandenrg, cnt,
                  (faac_real)aacquantCfg->quality/DEFQUAL);
            qlevel(coder, gxr, bandlvl, bandenrg, cnt, aacquantCfg->pnslevel, aacquantCfg);
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

        // fixme: move SF range check to quantizer
        clamp_sf(coder);
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
