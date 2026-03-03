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
#include <string.h>
#include "quantize.h"
#include "huff2.h"
#include "faac_real.h"

#if defined(HAVE_IMMINTRIN_H) && defined(CPUSSE)
# include <immintrin.h>
#endif

#ifdef __SSE2__
# ifdef __GNUC__
#  include <cpuid.h>
# endif
#endif

#ifdef _MSC_VER
# include <immintrin.h>
# include <intrin.h>
# define __SSE2__
# define bit_SSE2 (1 << 26)
#endif

#ifdef __SSE2__
#ifdef _MSC_VER /* visual c++ */
#define ALIGN16_BEG __declspec(align(16))
#define ALIGN16_END
#else /* gcc or icc */
#define ALIGN16_BEG
#define ALIGN16_END __attribute__((aligned(16)))
#endif
#else
#define ALIGN16_BEG
#define ALIGN16_END
#endif

#ifdef __GNUC__
#define GCC_VERSION (__GNUC__ * 10000 \
                     + __GNUC_MINOR__ * 100 \
                     + __GNUC_PATCHLEVEL__)
#endif

#define MAGIC_NUMBER  0.4054
#define NOISEFLOOR 0.4

// MDCT-based Psychoacoustic Model
static void bmask(GlobalPsyInfo *gpsyInfo, CoderInfo *coderInfo, const faac_real *xr0, faac_real *bandqual,
                  faac_real *enrg, faac_real *maxe,
                  int gnum, faac_real quality, int rate)
{
  int sfb, start, end, win;
  int gsize = coderInfo->groups.len[gnum];
  const faac_real *xr;
  faac_real thr[NSFB_LONG];
  faac_real total_enrg = 0.0;
  int num_bands = coderInfo->sfbn;
  int enrgcnt = 0;

  // 1. Energy and Peak Estimation
  // Standard AAC psychoacoustic energy estimation in MDCT domain.
  // Re-ordered loops and removed redundant start offset lookups for better cache locality and performance (Portable improvement).
  for (sfb = 0; sfb < num_bands; sfb++) {
      enrg[sfb] = 0.0;
      maxe[sfb] = 0.0;
  }

  xr = xr0;
  for (win = 0; win < gsize; win++) {
      int line = 0;
      for (sfb = 0; sfb < num_bands; sfb++) {
          int end_line = coderInfo->sfb_offset[sfb + 1];
          for (; line < end_line; line++) {
              faac_real e = xr[line] * xr[line];
              enrg[sfb] += e;
              if (maxe[sfb] < e) maxe[sfb] = e;
          }
      }
      xr += BLOCK_LEN_SHORT;
  }

  for (sfb = 0; sfb < num_bands; sfb++) {
      total_enrg += enrg[sfb];
      maxe[sfb] *= gsize;
  }
  enrgcnt = coderInfo->sfb_offset[num_bands] * gsize;

  // 2. Silence detection matching baseline
  if (total_enrg < ((NOISEFLOOR * NOISEFLOOR) * (faac_real)enrgcnt)) {
      for (sfb = 0; sfb < num_bands; sfb++) bandqual[sfb] = 0.0;
      return;
  }

  int last = (coderInfo->block_type == ONLY_SHORT_WINDOW) ? BLOCK_LEN_SHORT : BLOCK_LEN_LONG;

  // 3. Masking Threshold Calculation
  // Pre-calculate constant factor to remove divisions from the loop
  faac_real inv_total_enrg_last = (total_enrg > 1e-9) ? (faac_real)last / total_enrg : 0.0;

  for (sfb = 0; sfb < num_bands; sfb++) {
      start = coderInfo->sfb_offset[sfb];
      end = coderInfo->sfb_offset[sfb + 1];

      faac_real target;
      if (inv_total_enrg_last > 0.0) {
          faac_real inv_width = (coderInfo->block_type == ONLY_SHORT_WINDOW) ? gpsyInfo->inv_width_short[sfb] : gpsyInfo->inv_width_long[sfb];
          faac_real inv_avgenrg = inv_total_enrg_last * inv_width;
          faac_real rel_avg = enrg[sfb] * inv_avgenrg;
          faac_real rel_max = maxe[sfb] * inv_avgenrg;

          target = 0.2 * FAAC_POW(rel_avg, 0.4) + 0.36 * FAAC_POW(rel_max, 0.4);
      } else {
          target = 0.01;
      }

      // 4. Heuristic frequency weighting matching validated baseline performance.
      faac_real freq_scale = 10.0 / (1.0 + ((faac_real)(start+end)/last));
      target *= freq_scale;

      if (coderInfo->block_type == ONLY_SHORT_WINDOW)
          target *= 1.5;

      thr[sfb] = target * quality;

      // 5. ATH incorporation using precomputed LUT (ISO/IEC 13818-7 recommendation: Absolute Threshold of Hearing).
      faac_real ath_thr = (coderInfo->block_type == ONLY_SHORT_WINDOW) ? gpsyInfo->ath_short[sfb] : gpsyInfo->ath_long[sfb];
      if (thr[sfb] < ath_thr) thr[sfb] = ath_thr;
  }

  // 6. Subtle spreading function
  // Implements frequency-domain masking spreading (ISO/IEC 13818-7 deviation: simplified for CPU performance).
  for (sfb = 1; sfb < num_bands; sfb++) {
      faac_real spread = thr[sfb-1] * 0.1;
      if (thr[sfb] < spread) thr[sfb] = spread;
  }
  for (sfb = num_bands - 2; sfb >= 0; sfb--) {
      faac_real spread = thr[sfb+1] * 0.05;
      if (thr[sfb] < spread) thr[sfb] = spread;
  }

  for (sfb = 0; sfb < num_bands; sfb++) {
      bandqual[sfb] = thr[sfb];
  }
}

enum {MAXSHORTBAND = 36};
// use band quality levels to quantize a group of windows
static void qlevel(GlobalPsyInfo *gpsyInfo,
                   CoderInfo *coderInfo,
                   const faac_real *xr0,
                   const faac_real *bandqual,
                   const faac_real *enrg,
                   int gnum,
                   int pnslevel
                  )
{
    int sb, cnt;
    /* 2^0.25 (1.50515 dB) step from ISO/IEC 13818-7 AAC specs */
#if !defined(__clang__) && defined(__GNUC__) && (GCC_VERSION >= 40600)
    static const faac_real sfstep = 1.0 / FAAC_LOG10(FAAC_SQRT(FAAC_SQRT(2.0)));
#else
    static const faac_real sfstep = 20.0 / 1.50515;
#endif
    int gsize = coderInfo->groups.len[gnum];
#ifndef DRM
    faac_real pnsthr = 0.1 * pnslevel;
#endif
#ifdef __SSE2__
    int cpuid[4];
    int sse2 = 0;

    cpuid[3] = 0;
# ifdef __GNUC__
    __cpuid(1, cpuid[0], cpuid[1], cpuid[2], cpuid[3]);
# endif
# ifdef _MSC_VER
    __cpuid(cpuid, 1);
# endif
    if (cpuid[3] & bit_SSE2)
        sse2 = 1;
#endif

    for (sb = 0; sb < coderInfo->sfbn; sb++)
    {
      faac_real sfacfix;
      int sfac;
      faac_real rmsx;
      faac_real etot;
      int ALIGN16_BEG xitab[8 * MAXSHORTBAND] ALIGN16_END;
      int *xi;
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

      /* Reuse precalculated energy from bmask (Portable performance improvement) */
      etot = enrg[sb] / (faac_real)gsize;
      faac_real inv_width = (coderInfo->block_type == ONLY_SHORT_WINDOW) ? gpsyInfo->inv_width_short[sb] : gpsyInfo->inv_width_long[sb];
      rmsx = FAAC_SQRT(etot * inv_width);

      if ((rmsx < NOISEFLOOR) || (!bandqual[sb]))
      {
          coderInfo->book[coderInfo->bandcnt++] = HCB_ZERO;
          continue;
      }

#ifndef DRM
      if (bandqual[sb] < pnsthr)
      {
          coderInfo->book[coderInfo->bandcnt] = HCB_PNS;
          coderInfo->sf[coderInfo->bandcnt] +=
              FAAC_LRINT(FAAC_LOG10(etot) * (0.5 * sfstep));
          coderInfo->bandcnt++;
          continue;
      }
#endif

      /* Calculates the scalefactor (sfac) according to ISO/IEC 13818-7: 2^0.25 steps. */
      sfac = FAAC_LRINT(FAAC_LOG10(bandqual[sb] / rmsx) * sfstep);

      // Protect against Huffman overflow and underflow (-100 to 100 range)
      if (sfac < -100) sfac = -100;
      if (sfac > 100) sfac = 100;

      /* Use precomputed sfacfix LUT to save pow calls */
      sfacfix = gpsyInfo->sfacfix_lut[sfac + 100];

      xr = xr0 + start;
      end -= start;
      xi = xitab;
      for (win = 0; win < gsize; win++)
      {
#ifdef __SSE2__
          if (sse2)
          {
              const __m128 zero = _mm_setzero_ps();
              const __m128 sfac = _mm_set1_ps(sfacfix);
              const __m128 magic = _mm_set1_ps(MAGIC_NUMBER);
              for (cnt = 0; cnt < end; cnt += 4)
              {
                  __m128 x = {xr[cnt], xr[cnt + 1], xr[cnt + 2], xr[cnt + 3]};

                  x = _mm_max_ps(x, _mm_sub_ps(zero, x));
                  x = _mm_mul_ps(x, sfac);
                  x = _mm_mul_ps(x, _mm_sqrt_ps(x));
                  x = _mm_sqrt_ps(x);
                  x = _mm_add_ps(x, magic);

                  *(__m128i*)(xi + cnt) = _mm_cvttps_epi32(x);
              }
              for (cnt = 0; cnt < end; cnt++)
              {
                  if (xr[cnt] < 0)
                      xi[cnt] = -xi[cnt];
              }
              xi += cnt;
              xr += BLOCK_LEN_SHORT;
              continue;
          }
#endif

          for (cnt = 0; cnt < end; cnt++)
          {
              faac_real tmp = FAAC_FABS(xr[cnt]);

              tmp *= sfacfix;
              tmp = FAAC_SQRT(tmp * FAAC_SQRT(tmp));

              xi[cnt] = (int)(tmp + MAGIC_NUMBER);
              if (xr[cnt] < 0)
                  xi[cnt] = -xi[cnt];
          }
          xi += cnt;
          xr += BLOCK_LEN_SHORT;
      }
      huffbook(coderInfo, xitab, gsize * end);
      coderInfo->sf[coderInfo->bandcnt++] += SF_OFFSET - sfac;
    }
}

int BlocQuant(GlobalPsyInfo *gpsyInfo, CoderInfo *coder, faac_real *xr, AACQuantCfg *aacquantCfg, int rate)
{
    faac_real bandlvl[MAX_SCFAC_BANDS];
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
            faac_real enrg[NSFB_LONG];
            faac_real maxe[NSFB_LONG];

            bmask(gpsyInfo, coder, gxr, bandlvl, enrg, maxe, cnt,
                  (faac_real)aacquantCfg->quality/DEFQUAL, rate);
            qlevel(gpsyInfo, coder, gxr, bandlvl, enrg, cnt, aacquantCfg->pnslevel);
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

static void calce(faac_real *xr, int *bands, faac_real e[NSFB_SHORT], int maxsfb,
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
