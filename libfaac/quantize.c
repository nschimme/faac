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

static void compute_masking_thresholds(CoderInfo *coder, faac_real *xr0, faac_real *thresh, faac_real *enrg, int gnum, int sr_idx);

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
#define NOISEFLOOR 0.4

// band sound masking
static void bmask(CoderInfo * __restrict coderInfo, faac_real * __restrict xr0, faac_real * __restrict bandqual,
                  faac_real * __restrict bandenrg, int gnum, faac_real quality, int sr_idx)
{
    faac_real thresh[MAX_SCFAC_BANDS];
    compute_masking_thresholds(coderInfo, xr0, thresh, bandenrg, gnum, sr_idx);

    for (int sfb = 0; sfb < coderInfo->sfbn; sfb++) {
        /* Map masking energy threshold to target RMS amplitude.
           Scale based on theoretical noise floor (sqrt(12)) and quality factor.
           Quality 1.0 is default. */
        bandqual[sfb] = (faac_real)sqrt(thresh[sfb] + 1e-10) * quality * 2.5f;
    }
}

enum {MAXSHORTBAND = 36};
// use band quality levels to quantize a group of windows
static void qlevel(CoderInfo * __restrict coderInfo,
                   const faac_real * __restrict xr0,
                   const faac_real * __restrict bandqual,
                   const faac_real * __restrict bandenrg,
                   int gnum,
                   int pnslevel
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
      int xitab[8 * MAXSHORTBAND];
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

      etot = bandenrg[sb];
      rmsx = FAAC_SQRT(etot);

      if ((rmsx < NOISEFLOOR) || (!bandqual[sb]))
      {
          coderInfo->book[coderInfo->bandcnt++] = HCB_ZERO;
          continue;
      }

      if (bandqual[sb] < pnsthr)
      {
          coderInfo->book[coderInfo->bandcnt] = HCB_PNS;
          /* PNS energy scalefactor calculation from original logic */
          coderInfo->sf[coderInfo->bandcnt] +=
              FAAC_LRINT(FAAC_LOG10(etot * gsize * (end - start) + 1e-10) * (0.5 * sfstep));
          coderInfo->bandcnt++;
          continue;
      }

      /* sfac = 20 * log10(target_error / signal) / 1.5 */
      sfac = FAAC_LRINT(FAAC_LOG10(bandqual[sb] / (rmsx + 1e-10)) * sfstep);

      /* Find max absolute value in this band to prevent overflow */
      faac_real max_xr = 0.0;
      for (win = 0; win < gsize; win++) {
          const faac_real *xr_win = xr0 + win * BLOCK_LEN_SHORT + start;
          for (int i = 0; i < (end - start); i++) {
              faac_real abs_xr = FAAC_FABS(xr_win[i]);
              if (abs_xr > max_xr) max_xr = abs_xr;
          }
      }

      /* Clamp sfac to prevent quantized values from exceeding 8191. */
      int sfac_max = FAAC_LRINT(FAAC_LOG10(165000.0f / (max_xr + 1e-15)) * sfstep);
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
      coderInfo->sf[coderInfo->bandcnt++] += SF_OFFSET - sfac;
    }
}

static PsyTable cached_psy_tables[13];
static int psy_tables_init[13] = {0};

static void compute_masking_thresholds(CoderInfo *coder, faac_real *xr0, faac_real *thresh, faac_real *enrg, int gnum, int sr_idx)
{
    int sfb, start, end, cnt, win;
    int *cb_offset = coder->sfb_offset;
    int gsize = coder->groups.len[gnum];
    faac_real *xr;
    float sfb_ath[NSFB_LONG];
    float spread_enrg[NSFB_LONG];
    PsyTable *psyTable;
    extern SR_INFO srInfo[];

    if (sr_idx < 0 || sr_idx > 12) return;

    if (!psy_tables_init[sr_idx]) {
        precompute_psy_tables(&cached_psy_tables[sr_idx], &srInfo[sr_idx]);
        psy_tables_init[sr_idx] = 1;
    }
    psyTable = &cached_psy_tables[sr_idx];

    if (coder->block_type == ONLY_SHORT_WINDOW) {
        for (sfb = 0; sfb < coder->sfbn; sfb++) {
            sfb_ath[sfb] = psyTable->sfb_ath_s[sfb];
        }
    } else {
        for (sfb = 0; sfb < coder->sfbn; sfb++) {
            sfb_ath[sfb] = psyTable->sfb_ath[sfb];
        }
    }

    /* 1. Calculate energy per band (normalized per line) */
    for (sfb = 0; sfb < coder->sfbn; sfb++) {
        start = cb_offset[sfb];
        end = cb_offset[sfb + 1];
        enrg[sfb] = 0.0;
        for (win = 0; win < gsize; win++) {
            xr = xr0 + win * BLOCK_LEN_SHORT + start;
            for (cnt = 0; cnt < (end - start); cnt++) {
                enrg[sfb] += xr[cnt] * xr[cnt];
            }
        }
        enrg[sfb] /= (faac_real)(gsize * (end - start) + 1e-10);
    }

    /* 2. Apply Spreading Function in Bark domain */
    if (coder->block_type == ONLY_SHORT_WINDOW) {
        for (sfb = 0; sfb < coder->sfbn; sfb++) {
            float linear_spread = 0.0f;
            for (int j = 0; j < coder->sfbn; j++) {
                linear_spread += (float)enrg[j] * psyTable->spread_short[sfb][j];
            }
            spread_enrg[sfb] = linear_spread;
        }
    } else {
        for (sfb = 0; sfb < coder->sfbn; sfb++) {
            float linear_spread = 0.0f;
            for (int j = 0; j < coder->sfbn; j++) {
                linear_spread += (float)enrg[j] * psyTable->spread_low[sfb][j];
            }
            spread_enrg[sfb] = linear_spread;
        }
    }

    /* 3. Threshold = max(spread, ATH) */
    for (sfb = 0; sfb < coder->sfbn; sfb++) {
        /* ATH scaled to MDCT domain. Reference: 0.16 matched to legacy noise floor. */
        float ath_linear = 0.16f * powf(10.0f, sfb_ath[sfb] / 10.0f);
        /* Apply 10dB SMR offset to spreading (0.1) */
        float spread_threshold = spread_enrg[sfb] * 0.1f;
        thresh[sfb] = (spread_threshold > ath_linear) ? spread_threshold : ath_linear;
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
        int lastis;
        int lastsf;

        gxr = xr;
        for (cnt = 0; cnt < coder->groups.n; cnt++)
        {
            bmask(coder, gxr, bandlvl, bandenrg, cnt,
                  (faac_real)aacquantCfg->quality/DEFQUAL, aacquantCfg->sr_idx);
            qlevel(coder, gxr, bandlvl, bandenrg, cnt, aacquantCfg->pnslevel);
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
            else if (book == HCB_ESC)
            {
                int diff = coder->sf[cnt] - lastsf;
                if (diff < -60) diff = -60;
                if (diff > 60) diff = 60;
                lastsf += diff;
                coder->sf[cnt] = lastsf;
            }
        }
        return 1;
    }
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
            if (min[sfb] > e[sfb]) min[sfb] = e[sfb];
            if (max[sfb] < e[sfb]) max[sfb] = e[sfb];
            if (max[sfb] > thr * min[sfb]) fast++;
        }
        if (fast > fastmin)
        {
            coderInfo->groups.len[coderInfo->groups.n++] = win - win0;
            win0 = win;
            resete(min, max, e, maxsfb);
        }
    }
    coderInfo->groups.len[coderInfo->groups.n++] = win - win0;
}

void BlocStat(void) {}
