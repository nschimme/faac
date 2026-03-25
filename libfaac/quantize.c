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
#include "psy_tables.h"

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

void QuantizeInit(void)
{
#if defined(HAVE_SSE2)
    CPUCaps caps = get_cpu_caps();
    if (caps & CPU_CAP_SSE2)
        qfunc = quantize_sse2;
    else
#endif
        qfunc = quantize_scalar;
}

/* Numerical stability epsilons */
#define EPS_ENRG ((faac_real)1e-15)
#define EPS_SMR  ((faac_real)1e-10)

/* Reference energy floor for global silence gate (per line) */
#define LEGACY_ENRG_FLOOR 0.16f

/* Spreading function offset (dB). ISO Model 2 suggests ~6-10 dB for low bitrates.
 * Set to 12.0f to be conservative and prevent MOS regressions. */
#define SPREAD_OFFSET 12.0f

/* ATH scaling to align with legacy NOISEFLOOR 0.4 rms at 1kHz. */
#define ATH_LINEAR_SCALE 0.0734f

/* Absolute Threshold of Hearing (ATH) scaling for per-band zero-out logic.
 * 0.1 corresponds to a -10 dB offset from the theoretical ATH floor. */
#define ATH_ZERO_SCALE 0.1f

/* Quantization upper bound to keep x_quant < 8192 (Huffman limit).
 * x_quant = (abs(xr) * sfacfix)^0.75 + 0.4054
 * 8191.5 ^ (4/3) is approx 165000.
 * This ensures that the quantized value 'q' in quantize_scalar does not
 * exceed the 13-bit escape coding limit of the AAC Huffman books. */
#define QUANT_UPPER_BOUND 165000.0f


// band sound masking
static void bmask(CoderInfo * __restrict coderInfo, faac_real * __restrict xr0, faac_real * __restrict bandqual,
                  faac_real * __restrict bandenrg, faac_real * __restrict bandmax, int gnum, faac_real quality,
                  int sr_idx, int spreading)
{
  int sfb, start, end, cnt, m;
  int *cb_offset = coderInfo->sfb_offset;
  int last;
  faac_real avgenrg;
  faac_real powm = 0.4;
  faac_real totenrg = 0.0;
  int gsize = coderInfo->groups.len[gnum];
  const faac_real *xr;
  int win;
  int total_len = cb_offset[coderInfo->sfbn];
  int is_short = (coderInfo->block_type == ONLY_SHORT_WINDOW);
  const float *bark = is_short ? sfb_bark_s[sr_idx] : sfb_bark[sr_idx];
  const float *ath  = is_short ? sfb_ath_s[sr_idx]  : sfb_ath[sr_idx];
  double frame_ath_thr = (double)LEGACY_ENRG_FLOOR * total_len * gsize;

  if (spreading) {
      frame_ath_thr = 0.0;
      /* For low bitrates, use a more precise ATH-based global silence gate.
       * We use a -10 dB offset from ATH for the global gate to be safe. */
      for (sfb = 0; sfb < coderInfo->sfbn; sfb++) {
          float ath_linear_per_line = ATH_LINEAR_SCALE * powf(10.0f, (ath[sfb] - 10.0f) * 0.1f);
          frame_ath_thr += (double)ath_linear_per_line * (cb_offset[sfb+1] - cb_offset[sfb]) * gsize;
      }
  }

  for (win = 0; win < gsize; win++)
  {
      xr = xr0 + win * BLOCK_LEN_SHORT;
      for (cnt = 0; cnt < total_len; cnt++)
      {
          totenrg += xr[cnt] * xr[cnt];
      }
  }

  if (totenrg < frame_ath_thr)
  {
      for (sfb = 0; sfb < coderInfo->sfbn; sfb++)
      {
          bandqual[sfb] = 0.0;
          bandenrg[sfb] = 0.0;
          bandmax[sfb] = 0.0;
      }
      return;
  }

  for (sfb = 0; sfb < coderInfo->sfbn; sfb++)
  {
    faac_real avge, maxe_enrg, maxe_abs;
    faac_real target;

    start = cb_offset[sfb];
    end = cb_offset[sfb + 1];

    avge = 0.0;
    maxe_enrg = 0.0;
    maxe_abs = 0.0;
    for (win = 0; win < gsize; win++)
    {
        xr = xr0 + win * BLOCK_LEN_SHORT + start;
        int n = end - start;
        for (cnt = 0; cnt < n; cnt++)
        {
            faac_real val = xr[cnt];
            faac_real abs_val = FAAC_FABS(val);
            faac_real e = val * val;
            avge += e;
            if (maxe_enrg < e)
                maxe_enrg = e;
            if (maxe_abs < abs_val)
                maxe_abs = abs_val;
        }
    }
    bandenrg[sfb] = avge;
    bandmax[sfb] = maxe_abs;
    maxe_enrg *= gsize;

#define NOISETONE 0.2
    if (coderInfo->block_type == ONLY_SHORT_WINDOW)
    {
        last = BLOCK_LEN_SHORT;
        avgenrg = totenrg / last;
        avgenrg *= end - start;

        target = NOISETONE * FAAC_POW(avge/avgenrg, powm);
        target += (1.0 - NOISETONE) * 0.45 * FAAC_POW(maxe_enrg/avgenrg, powm);

        target *= 1.5;
    }
    else
    {
        last = BLOCK_LEN_LONG;
        avgenrg = totenrg / last;
        avgenrg *= end - start;

        target = NOISETONE * FAAC_POW(avge/avgenrg, powm);
        target += (1.0 - NOISETONE) * 0.45 * FAAC_POW(maxe_enrg/avgenrg, powm);
    }

    target *= 10.0 / (1.0 + ((faac_real)(start+end)/last));

    bandqual[sfb] = target * quality;
  }

  if (!spreading)
    return;

  for (sfb = 0; sfb < coderInfo->sfbn; sfb++)
  {
    float b_t = bark[sfb];
    double M  = 0.0;
    float ath_thr;

    /* 1. Spreading function masking (inter-band) */
    for (m = 0; m < coderInfo->sfbn; m++)
    {
      float dz = b_t - bark[m];
      /* ISO AAC Model 2 spreading function weight */
      float weight = 15.811389f + 7.5f * (dz + 0.474f) - 17.5f * sqrtf(1.0f + (dz + 0.474f) * (dz + 0.474f));
      M += (double)bandenrg[m] * powf(10.0f, (weight - SPREAD_OFFSET) * 0.1f);
    }

    /* 2. Absolute Threshold of Hearing (ATH) masking floor (per-band zero-out) */
    ath_thr = ATH_ZERO_SCALE * ATH_LINEAR_SCALE * powf(10.0f, ath[sfb] * 0.1f) * (cb_offset[sfb+1] - cb_offset[sfb]) * gsize;

    if ((double)bandenrg[sfb] <= M || bandenrg[sfb] < ath_thr)
      bandqual[sfb] = 0.0;
  }
}

enum {MAXSHORTBAND = 36};
// use band quality levels to quantize a group of windows

// use band quality levels to quantize a group of windows
static void qlevel(CoderInfo * __restrict coderInfo,
                   const faac_real * __restrict xr0,
                   const faac_real * __restrict bandqual,
                   const faac_real * __restrict bandenrg,
                   const faac_real * __restrict bandmax,
                   const float *ath,
                   int gnum,
                   int pnslevel,
                   int spreading
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
      faac_real sfacfix;
      int sfac;
      faac_real rmsx;
      faac_real etot;
      faac_real local_noisefloor;
      int xitab[8 * MAXSHORTBAND];
      int *xi;
      int start, end;
      const faac_real *xr;
      int win;

      /* Frequency-dependent noise floor derived from ATH for low bitrates.
       * Fallback to legacy constant for high bitrates to maintain bit-identity. */
      local_noisefloor = spreading ? FAAC_SQRT(ATH_LINEAR_SCALE * powf(10.0f, ath[sb] * 0.1f)) : (faac_real)0.4;

      if (coderInfo->book[coderInfo->bandcnt] != HCB_NONE)
      {
          coderInfo->bandcnt++;
          continue;
      }

      start = coderInfo->sfb_offset[sb];
      end = coderInfo->sfb_offset[sb+1];

      etot = bandenrg[sb] / (faac_real)gsize;
      rmsx = FAAC_SQRT(etot / (end - start) + EPS_ENRG);

      if ((rmsx < local_noisefloor) || (!bandqual[sb]))
      {
          coderInfo->book[coderInfo->bandcnt++] = HCB_ZERO;
          continue;
      }

      if (bandqual[sb] < pnsthr)
      {
          coderInfo->book[coderInfo->bandcnt] = HCB_PNS;
          /* PNS energy represents power per window. Use average energy 'etot'. */
          coderInfo->sf[coderInfo->bandcnt] =
              FAAC_LRINT(FAAC_LOG10(etot + EPS_SMR) * (0.5 * sfstep));
          coderInfo->bandcnt++;
          continue;
      }

      sfac = FAAC_LRINT(FAAC_LOG10(bandqual[sb] / rmsx) * sfstep);

      /* Clamp sfac to prevent quantized values from exceeding Huffman limit. */
      int sfac_max = FAAC_LRINT(FAAC_LOG10(QUANT_UPPER_BOUND / (bandmax[sb] + EPS_ENRG)) * sfstep);
      if (sfac > sfac_max) sfac = sfac_max;

      if ((SF_OFFSET - sfac) < 10)
          sfacfix = 0.0;
      else
          sfacfix = FAAC_POW(10, sfac / sfstep);

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
      coderInfo->sf[coderInfo->bandcnt++] = SF_OFFSET - sfac;
    }
}

int BlocQuant(CoderInfo * __restrict coder, faac_real * __restrict xr, AACQuantCfg *aacquantCfg)
{
    faac_real bandlvl[MAX_SCFAC_BANDS];
    faac_real bandenrg[MAX_SCFAC_BANDS];
    faac_real bandmax[MAX_SCFAC_BANDS];
    int cnt;
    faac_real *gxr;

    coder->global_gain = 0;

    coder->bandcnt = 0;
    coder->datacnt = 0;

    {
        int lastis;
        int lastsf;
        int lastpns;

        gxr = xr;
        for (cnt = 0; cnt < coder->groups.n; cnt++)
        {
            bmask(coder, gxr, bandlvl, bandenrg, bandmax, cnt,
                  (faac_real)aacquantCfg->quality/DEFQUAL,
                  aacquantCfg->sr_idx,
                  aacquantCfg->spreading);
            qlevel(coder, gxr, bandlvl, bandenrg, bandmax,
                   (coder->block_type == ONLY_SHORT_WINDOW) ? sfb_ath_s[aacquantCfg->sr_idx] : sfb_ath[aacquantCfg->sr_idx],
                   cnt, aacquantCfg->pnslevel, aacquantCfg->spreading);
            gxr += coder->groups.len[cnt] * BLOCK_LEN_SHORT;
        }

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

        lastsf = coder->global_gain;
        lastis = 0;
        lastpns = coder->global_gain - 90;

        {
            int initpns = 1;
            for (cnt = 0; cnt < coder->bandcnt; cnt++)
            {
                int book = coder->book[cnt];
                if ((book == HCB_INTENSITY) || (book == HCB_INTENSITY2))
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
                    if (initpns)
                    {
                        initpns = 0;
                    }
                    else
                    {
                        if (diff < -60) diff = -60;
                        if (diff > 60) diff = 60;
                    }
                    lastpns += diff;
                    coder->sf[cnt] = lastpns;
                }
                else if (book != HCB_ZERO)
                {
                    int diff = coder->sf[cnt] - lastsf;
                    if (diff < -60) diff = -60;
                    if (diff > 60) diff = 60;
                    lastsf += diff;
                    coder->sf[cnt] = lastsf;
                }
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
