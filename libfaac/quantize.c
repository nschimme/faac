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

typedef void (*QuantizeFunc)(const faac_real * __restrict xr, int * __restrict xi, int n, faac_real sfacfix, faac_real magic);

#if defined(HAVE_SSE2)
extern void quantize_sse2(const faac_real * __restrict xr, int * __restrict xi, int n, faac_real sfacfix, faac_real magic);
#endif

static void quantize_scalar(const faac_real * __restrict xr, int * __restrict xi, int n, faac_real sfacfix, faac_real magic)
{
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

void QuantizeSaveState(CoderInfo *coder)
{
    coder->saved_global_gain = coder->global_gain;
    memcpy(coder->saved_sf, coder->sf, sizeof(int) * MAX_SCFAC_BANDS);
    memcpy(coder->saved_book, coder->book, sizeof(int) * MAX_SCFAC_BANDS);
    coder->saved_bandcnt = coder->bandcnt;
    coder->saved_datacnt = coder->datacnt;
}

void QuantizeRestoreState(CoderInfo *coder)
{
    coder->global_gain = coder->saved_global_gain;
    memcpy(coder->sf, coder->saved_sf, sizeof(int) * MAX_SCFAC_BANDS);
    memcpy(coder->book, coder->saved_book, sizeof(int) * MAX_SCFAC_BANDS);
    coder->bandcnt = coder->saved_bandcnt;
    coder->datacnt = coder->saved_datacnt;
}

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
#define NOISEFLOOR 0.4

// band sound masking
static void bmask(CoderInfo * __restrict coderInfo, faac_real * __restrict xr0, faac_real * __restrict bandqual,
                  faac_real * __restrict bandenrg, int gnum, faac_real quality, int sampleRate, faac_real * __restrict bandtonal)
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
          bandtonal[sfb] = 0.0;
      }

      return;
  }

  /* Hoist block-type and sampling-rate dependent constants */
  last = (coderInfo->block_type == ONLY_SHORT_WINDOW) ? BLOCK_LEN_SHORT : BLOCK_LEN_LONG;
  const faac_real last_inv = 1.0 / (faac_real)last;
  const faac_real totenrg_last = totenrg * last_inv;
  const faac_real ath_adj = (sampleRate <= 16000) ? 0.8 : 1.0;
  const faac_real freq_factor = (faac_real)sampleRate * 0.25 * last_inv;

  for (sfb = 0; sfb < coderInfo->sfbn; sfb++)
  {
    faac_real avge, maxe;
    faac_real target;
    int n;

    start = cb_offset[sfb];
    end = cb_offset[sfb + 1];
    n = end - start;

    avge = 0.0;
    maxe = 0.0;
    for (win = 0; win < gsize; win++)
    {
        xr = xr0 + win * BLOCK_LEN_SHORT + start;
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

    // ISO/IEC 14496-3 Section 4.6.2: Scalefactor determination.
    // Calculate tonality as peak-to-average energy ratio.
    faac_real avg_sample_e = avge / (faac_real)(gsize * n > 0 ? gsize * n : 1);
    bandtonal[sfb] = maxe / (avg_sample_e > 1e-10 ? avg_sample_e : 1e-10);

    maxe *= gsize;
    avgenrg = totenrg_last * n;

#define NOISETONE 0.2
    target = NOISETONE * FAAC_POW(avge/avgenrg, powm) + 0.36 * FAAC_POW(maxe/avgenrg, powm);
    if (coderInfo->block_type == ONLY_SHORT_WINDOW) target *= 1.5;

    target *= 10.0 / (1.0 + ((faac_real)(start+end) * last_inv));

    if (sampleRate <= 16000) {
        // Refined ATH Scaling (VoIP/VSS): 1.25x scaling boost for 16kHz modes to prioritize speech clarity.
        // ISO/IEC 14496-3 Section 4.6.2.1: Deviation - Frequency-dependent ATH floor adjustment.
        target *= ath_adj; // effectively 1.25x boost as target is threshold (lower = more sensitive/more bits)
        faac_real freq = (faac_real)(start + end) * freq_factor;
        if (freq >= 300.0 && freq <= 4000.0) target *= 0.8;
    }

    bandqual[sfb] = target * quality;
  }

  /* ISO/IEC 14496-3 Section 4.6.2.2: Spreading function.
   * Authorized Expansion: Low-complexity upward masking.
   * Model how strong energy in one band masks the following band. */
  for (sfb = 1; sfb < coderInfo->sfbn; sfb++) {
      faac_real spread = bandqual[sfb - 1] * 0.2;
      if (bandqual[sfb] < spread) {
          bandqual[sfb] = spread;
      }
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
                   const faac_real * __restrict bandtonal
                  )
{
    int sb;
#if !defined(__clang__) && defined(__GNUC__) && (GCC_VERSION >= 40600)
    /* 2^0.25 (1.50515 dB) step from AAC specs */
    static const faac_real sfstep = 1.0 / FAAC_LOG10(FAAC_SQRT(FAAC_SQRT(2.0)));
#else
    static const faac_real sfstep = 20 / 1.50515;
#endif
    const int gsize = coderInfo->groups.len[gnum];
    const faac_real gsize_inv = 1.0 / (faac_real)gsize;
#ifndef DRM
    const faac_real pnsthr = 0.1 * pnslevel;
#endif

    for (sb = 0; sb < coderInfo->sfbn; sb++)
    {
      faac_real sfacfix;
      int sfac;
      faac_real rmsx;
      faac_real etot;
      int xitab[1024];
      int *xi;
      int start, end, n;
      const faac_real *xr;
      int win;

      if (coderInfo->book[coderInfo->bandcnt] != HCB_NONE)
      {
          coderInfo->bandcnt++;
          continue;
      }

      start = coderInfo->sfb_offset[sb];
      end = coderInfo->sfb_offset[sb+1];
      n = end - start;

      etot = bandenrg[sb] * gsize_inv;
      const int blk_len = gsize * n;
      rmsx = FAAC_SQRT(etot / n);

      if ((rmsx < NOISEFLOOR) || (!bandqual[sb]))
      {
          coderInfo->book[coderInfo->bandcnt++] = HCB_ZERO;
          continue;
      }

#ifndef DRM
      if (bandqual[sb] < pnsthr)
      {
          coderInfo->book[coderInfo->bandcnt] = HCB_PNS;
          coderInfo->sf[coderInfo->bandcnt] = FAAC_LRINT(FAAC_LOG10(etot) * (0.5 * sfstep));
          coderInfo->bandcnt++;
          continue;
      }
#endif

      sfac = FAAC_LRINT(FAAC_LOG10(bandqual[sb] / rmsx) * sfstep);
      if ((SF_OFFSET - sfac) < 0)
          sfacfix = 0.0;
      else
          sfacfix = FAAC_POW(10, sfac / sfstep);

      xi = xitab;
      if (sfacfix <= 0.0)
      {
          memset(xi, 0, blk_len * sizeof(int));
      }
      else
      {
          // Adaptive Quantization Rounding (AQR): Reduce rounding bias for high-frequency or noisy regions to reduce shimmer.
          // ISO/IEC 14496-3 Section 4.6.3: Deviation - Non-static rounding bias.
          faac_real magic = MAGIC_NUMBER;
          if (sb >= (int)(coderInfo->sfbn * 0.6)) {
              magic = 0.33;
          } else if (bandtonal[sb] > 3.0) {
              /* Smoothly transition bias: 0.4054 (tonal) -> 0.33 (noisy) as PAPR increases.
               * Tonal (low PAPR < 3.0) gets standard bias. Noisy (high PAPR > 5.0) gets reduced bias. */
              faac_real weight = (bandtonal[sb] - 3.0) * 0.5;
              if (weight < 0.0) weight = 0.0;
              if (weight > 1.0) weight = 1.0;
              magic = MAGIC_NUMBER - weight * (MAGIC_NUMBER - 0.33);
          }

          for (win = 0; win < gsize; win++)
          {
              xr = xr0 + win * BLOCK_LEN_SHORT + start;
              qfunc(xr, xi, n, sfacfix, magic);
              xi += n;
          }
      }
      huffbook(coderInfo, xitab, blk_len);
      coderInfo->sf[coderInfo->bandcnt++] = SF_OFFSET - sfac;
    }
}

int BlocQuant(CoderInfo * __restrict coder, faac_real * __restrict xr, AACQuantCfg *aacquantCfg, int sampleRate)
{
    faac_real bandlvl[MAX_SCFAC_BANDS];
    faac_real bandenrg[MAX_SCFAC_BANDS];
    faac_real bandtonal[MAX_SCFAC_BANDS];
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
        const faac_real qs = (faac_real)aacquantCfg->quality/DEFQUAL;

        gxr = xr;
        for (cnt = 0; cnt < coder->groups.n; cnt++)
        {
            int ofs = cnt * NSFB_SHORT;

            if (!coder->energies_valid) {
                bmask(coder, gxr, bandlvl, bandenrg, cnt, qs, sampleRate, bandtonal);
                // Cache energies and tonality for 2nd pass
                for (int i = 0; i < coder->sfbn; i++) {
                    coder->cached_bandqual[ofs + i] = bandlvl[i] / (qs > 1e-5 ? qs : 1e-5);
                }
                memcpy(coder->cached_bandenrg + ofs, bandenrg, coder->sfbn * sizeof(faac_real));
                memcpy(coder->cached_bandtonal + ofs, bandtonal, coder->sfbn * sizeof(faac_real));
            } else {
                // Use cached values
                memcpy(bandenrg, coder->cached_bandenrg + ofs, coder->sfbn * sizeof(faac_real));
                memcpy(bandtonal, coder->cached_bandtonal + ofs, coder->sfbn * sizeof(faac_real));
                for (int i = 0; i < coder->sfbn; i++) {
                    bandlvl[i] = coder->cached_bandqual[ofs + i] * qs;
                }
            }

            qlevel(coder, gxr, bandlvl, bandenrg, cnt, aacquantCfg->pnslevel, bandtonal);
            gxr += coder->groups.len[cnt] * BLOCK_LEN_SHORT;
        }

        /* ISO/IEC 14496-3 Section 4.6.2: Differential encoding of scalefactors. */
        coder->global_gain = 0;
        for (cnt = 0; cnt < coder->bandcnt; cnt++)
        {
            if (coder->book[cnt] > 0)
            {
                coder->global_gain = coder->sf[cnt];
                break;
            }
        }
        if (coder->global_gain < 0) coder->global_gain = 0;
        if (coder->global_gain > 255) coder->global_gain = 255;

        lastsf = coder->global_gain;
        lastis = 0;
        int lastpns = coder->global_gain - 90;
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
                if (lastis < -255) lastis = -255;
                if (lastis > 255) lastis = 255;
                coder->sf[cnt] = lastis;
            }
            else if (book == HCB_PNS)
            {
                int diff = coder->sf[cnt] - lastpns;
                if (initpns) {
                    /* First PNS band uses 9-bit absolute encoding (diff + 256) */
                    if (diff < -256) diff = -256;
                    if (diff > 255) diff = 255;
                    initpns = 0;
                } else {
                    if (diff < -60) diff = -60;
                    if (diff > 60) diff = 60;
                }
                lastpns += diff;
                coder->sf[cnt] = lastpns;
            }
            else if (book > 0 && book <= 11)
            {
                int diff = coder->sf[cnt] - lastsf;
                if (diff < -60) diff = -60;
                if (diff > 60) diff = 60;
                lastsf += diff;
                if (lastsf < 0) lastsf = 0;
                if (lastsf > 255) lastsf = 255;
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
