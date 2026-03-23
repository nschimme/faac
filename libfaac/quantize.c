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

/* Absolute threshold of hearing, Terhardt approximation.
 * f_hz: band centre frequency. Returns dB SPL.
 * Called only at initialisation time (inside CalcBW), never per-frame. */
static faac_real ath_db(faac_real f_hz)
{
    faac_real f = f_hz * 0.001;          /* Hz → kHz */
    if (f < 0.02) f = 0.02;
    /* Gaussian centred at 3.3 kHz avoids a dependency on expf/exp. */
    faac_real gauss_arg = -0.6 * (f - 3.3) * (f - 3.3);
    return  3.64  * FAAC_POW(f, -0.8)
           - 6.5  * FAAC_POW((faac_real)2.71828182845, gauss_arg)
           + 0.001 * FAAC_POW(f,  4.0);
}

// band sound masking
static void bmask(CoderInfo * __restrict coderInfo,
                  faac_real  * __restrict xr0,
                  faac_real  * __restrict bandqual,
                  faac_real  * __restrict bandenrg,
                  int gnum, faac_real qscale, AACQuantCfg *aacquantCfg)
{
    int sfb, start, end, cnt;
    int *cb_offset  = coderInfo->sfb_offset;
    faac_real avgenrg;
    const faac_real powm        = aacquantCfg->powm;
    const faac_real noise_floor = aacquantCfg->noise_floor;
    const faac_real mmult       = aacquantCfg->masking_mult;
    faac_real totenrg = 0.0;
    int gsize     = coderInfo->groups.len[gnum];
    const faac_real *xr;
    int win, enrgcnt;
    int total_len   = coderInfo->sfb_offset[coderInfo->sfbn];
    const int is_short = (coderInfo->block_type == ONLY_SHORT_WINDOW);

    /* Select the ATH weight array matching the current block type */
    const faac_real *ath_w = is_short ? aacquantCfg->ath_s
                                      : aacquantCfg->ath_l;

    for (win = 0; win < gsize; win++) {
        xr = xr0 + win * BLOCK_LEN_SHORT;
        for (cnt = 0; cnt < total_len; cnt++)
            totenrg += xr[cnt] * xr[cnt];
    }
    enrgcnt = gsize * total_len;

    if (totenrg < (noise_floor * noise_floor * (faac_real)enrgcnt)) {
        for (sfb = 0; sfb < coderInfo->sfbn; sfb++) {
            bandqual[sfb] = 0.0;
            bandenrg[sfb] = 0.0;
        }
        return;
    }

    const int last = is_short ? BLOCK_LEN_SHORT : BLOCK_LEN_LONG;

    for (sfb = 0; sfb < coderInfo->sfbn; sfb++) {
        faac_real avge = 0.0, maxe = 0.0, target;

        start = cb_offset[sfb];
        end   = cb_offset[sfb + 1];

        for (win = 0; win < gsize; win++) {
            xr = xr0 + win * BLOCK_LEN_SHORT + start;
            int n = end - start;
            for (cnt = 0; cnt < n; cnt++) {
                faac_real e = xr[cnt] * xr[cnt];
                avge += e;
                if (maxe < e) maxe = e;
            }
        }
        bandenrg[sfb] = avge;
        maxe *= gsize;

        avgenrg = (totenrg / last) * (end - start);

#define NOISETONE 0.2
        target  =         NOISETONE  * FAAC_POW(avge / avgenrg, powm);
        target += (1.0 - NOISETONE) * 0.45 * FAAC_POW(maxe / avgenrg, powm);
#undef NOISETONE

        if (is_short) target *= 1.5;

        /* Frequency-position taper (identical shape to original 10.0 formula)
         * combined with ATH hearing weight.
         *
         * ath_w[sfb] == 1.0 below 4 kHz: no change to existing behaviour there.
         * ath_w[sfb] falls to ~0.67 at 10 kHz and ~0.21 at 15 kHz: quality
         * targets are proportionally reduced, saving bits on near-inaudible HF.
         *
         * This achieves the same goal as the patch's freq_penalty but is:
         *  (a) non-cumulative — applied once per band, not per iteration
         *  (b) physically motivated — follows ISO 226 ATH rather than a magic constant
         *  (c) sample-rate aware — weights recalculated per session in CalcBW       */
        target *= (mmult / (1.0 + (faac_real)(start + end) / last)) * ath_w[sfb];

        bandqual[sfb] = target * qscale;
    }
}

enum {MAXSHORTBAND = 36};
// use band quality levels to quantize a group of windows
static void qlevel(CoderInfo       * __restrict coderInfo,
                   const faac_real * __restrict xr0,
                   const faac_real * __restrict bandqual,
                   const faac_real * __restrict bandenrg,
                   int gnum,
                   AACQuantCfg *aacquantCfg)
{
    int sb;
#if !defined(__clang__) && defined(__GNUC__) && (GCC_VERSION >= 40600)
    static const faac_real sfstep = 1.0 / FAAC_LOG10(FAAC_SQRT(FAAC_SQRT(2.0)));
#else
    static const faac_real sfstep = 20 / 1.50515;
#endif
    int gsize = coderInfo->groups.len[gnum];
    const faac_real inv_gsize  = 1.0 / (faac_real)gsize;
    const faac_real noise_floor = aacquantCfg->noise_floor;
    const faac_real pnsthr     = 0.1 * aacquantCfg->pnslevel;

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

      etot = bandenrg[sb] * inv_gsize;           /* was: / (faac_real)gsize */
      rmsx = FAAC_SQRT(etot / (end - start));

      if ((rmsx < noise_floor) || (!bandqual[sb]))  /* was: NOISEFLOOR */
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
        const faac_real qscale = (faac_real)aacquantCfg->quality / DEFQUAL;
        for (cnt = 0; cnt < coder->groups.n; cnt++)
        {
            bmask(coder, gxr, bandlvl, bandenrg, cnt, qscale, aacquantCfg);
            qlevel(coder, gxr, bandlvl, bandenrg, cnt, aacquantCfg);
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
    int max = *bw * (BLOCK_LEN_SHORT << 1) / rate;
    int cnt;
    int l;

    l = 0;
    for (cnt = 0; cnt < sr->num_cb_short; cnt++) {
        if (l >= max) break;
        l += sr->cb_width_short[cnt];
    }
    aacquantCfg->max_cbs = cnt;
    if (aacquantCfg->pnslevel)
        *bw = (faac_real)l * rate / (BLOCK_LEN_SHORT << 1);

    max = *bw * (BLOCK_LEN_LONG << 1) / rate;
    l = 0;
    for (cnt = 0; cnt < sr->num_cb_long; cnt++) {
        if (l >= max) break;
        l += sr->cb_width_long[cnt];
    }
    aacquantCfg->max_cbl = cnt;
    aacquantCfg->max_l   = l;
    *bw = (faac_real)l * rate / (BLOCK_LEN_LONG << 1);

    /* ------------------------------------------------------------------
     * Precompute per-SFB ATH hearing weights.
     *
     * ath_l[i] / ath_s[i] is 1.0 below 4 kHz where hearing is most acute.
     * Above 4 kHz it falls following the ATH curve: each 1 dB of ATH rise
     * above the 4 kHz reference reduces the quality target by 0.25 dB.
     * At 10 kHz this gives ~0.67; at 15 kHz ~0.21.
     *
     * This replaces the patch's cumulative freq_penalty, which compounded
     * per iteration and silenced virtually all energy above ~10 SFBs.
     * ------------------------------------------------------------------ */
    {
        faac_real ath_ref = ath_db(4000.0);   /* ≈ -3.4 dB SPL */
        int band_l = 0, band_s = 0;
        int i;

        for (i = 0; i < NSFB_LONG; i++) {
            if (i < sr->num_cb_long) {
                /* Centre frequency of this SFB */
                faac_real f_c = (faac_real)(band_l * 2 + sr->cb_width_long[i])
                              * 0.5 * rate / (2.0 * BLOCK_LEN_LONG);
                band_l += sr->cb_width_long[i];

                if (f_c >= 4000.0) {
                    faac_real exc = (ath_db(f_c) - ath_ref) * 0.25;
                    if (exc < 0.0) exc = 0.0;
                    aacquantCfg->ath_l[i] = FAAC_POW(10.0, -exc / 20.0);
                } else {
                    aacquantCfg->ath_l[i] = 1.0;
                }
            } else {
                aacquantCfg->ath_l[i] = 1.0;   /* unused bands: safe default */
            }
        }

        for (i = 0; i < NSFB_SHORT; i++) {
            if (i < sr->num_cb_short) {
                faac_real f_c = (faac_real)(band_s * 2 + sr->cb_width_short[i])
                              * 0.5 * rate / (2.0 * BLOCK_LEN_SHORT);
                band_s += sr->cb_width_short[i];

                if (f_c >= 4000.0) {
                    faac_real exc = (ath_db(f_c) - ath_ref) * 0.25;
                    if (exc < 0.0) exc = 0.0;
                    aacquantCfg->ath_s[i] = FAAC_POW(10.0, -exc / 20.0);
                } else {
                    aacquantCfg->ath_s[i] = 1.0;
                }
            } else {
                aacquantCfg->ath_s[i] = 1.0;
            }
        }
    }
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
