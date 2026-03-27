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
        /* Nonlinear quantization x^0.75 */
        tmp = FAAC_SQRT(tmp * FAAC_SQRT(tmp));

        int q = (int)(tmp + magic);
        if (q > 8191) q = 8191;
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

void FaacComputeMaskingThresholds(CoderInfo * __restrict coderInfo, faac_real * __restrict xr0, faac_real * __restrict bandqual,
                                        faac_real * __restrict bandenrg, faac_real * __restrict bandmax, int gnum, faac_real quality, GlobalPsyInfo *gpsyInfo)
{
    int sfb, j, start, end, cnt;
    int *cb_offset = coderInfo->sfb_offset;
    int gsize = coderInfo->groups.len[gnum];
    const faac_real *xr;
    int win;
    FaacPsyContext *ctx = gpsyInfo->psyContext;
    faac_real *ath = (coderInfo->block_type == ONLY_SHORT_WINDOW) ? ctx->ath_short : ctx->ath_long;
    faac_real *spread = (coderInfo->block_type == ONLY_SHORT_WINDOW) ? ctx->spread_matrix_short : ctx->spread_matrix_long;
    int stride = (coderInfo->block_type == ONLY_SHORT_WINDOW) ? NSFB_SHORT_H : NSFB_LONG_H;
    faac_real enrg[MAX_SCFAC_BANDS];

    for (sfb = 0; sfb < coderInfo->sfbn; sfb++) {
        start = cb_offset[sfb];
        end = cb_offset[sfb + 1];
        faac_real avge = 0.0;
        faac_real maxe = 0.0;
        for (win = 0; win < gsize; win++) {
            xr = xr0 + win * BLOCK_LEN_SHORT + start;
            int n = end - start;
            for (cnt = 0; cnt < n; cnt++) {
                faac_real val = FAAC_FABS(xr[cnt]);
                avge += val * val;
                if (maxe < val) maxe = val;
            }
        }
        if (bandenrg) bandenrg[sfb] = avge;
        enrg[sfb] = avge;
        if (bandmax) bandmax[sfb] = maxe;
    }

    for (sfb = 0; sfb < coderInfo->sfbn; sfb++) {
        faac_real mask = 0.0;
        for (j = 0; j < coderInfo->sfbn; j++) {
            mask += enrg[j] * spread[sfb * stride + j];
        }

        faac_real thr = mask;
        if (thr < ath[sfb]) thr = ath[sfb];

        /* Frequency-dependent silence gate floor */
        faac_real local_noisefloor = ath[sfb] * 0.1f;
        if (thr < local_noisefloor) thr = local_noisefloor;

        bandqual[sfb] = thr;
    }
}

static int ClampDiff(int val, int ref) {
    int diff = val - ref;
    if (diff < -60) diff = -60;
    if (diff > 60) diff = 60;
    return ref + diff;
}

// Pass 1: Suggest scale factors based on legacy libfaac SNR model
static void qlevel(CoderInfo * __restrict coderInfo,
                   const faac_real * __restrict xr0,
                   const faac_real * __restrict bandqual,
                   const faac_real * __restrict bandenrg,
                   const faac_real * __restrict bandmax,
                   int gnum,
                   faac_real quality,
                   int pnslevel
                  )
{
    int sb;
#if !defined(__clang__) && defined(__GNUC__) && (GCC_VERSION >= 40600)
    static const faac_real sfstep = 1.0 / FAAC_LOG10(FAAC_SQRT(FAAC_SQRT(2.0)));
#else
    static const faac_real sfstep = 20 / 1.50515;
#endif
    int gsize = coderInfo->groups.len[gnum];
    faac_real pnsthr = 0.1 * pnslevel;
    faac_real q_mult = quality / 100.0f;

    for (sb = 0; sb < coderInfo->sfbn; sb++)
    {
      faac_real etot;
      int start, end;

      if (coderInfo->book[coderInfo->bandcnt] != HCB_NONE)
      {
          coderInfo->bandcnt++;
          continue;
      }

      start = coderInfo->sfb_offset[sb];
      end = coderInfo->sfb_offset[sb+1];
      int n_lines = end - start;

      etot = bandenrg[sb] / (faac_real)gsize;

      faac_real thr_line = (bandqual[sb] * q_mult) / (faac_real)(gsize * n_lines);

      if (!bandqual[sb] || (etot < (bandqual[sb] * q_mult / gsize)))
      {
          coderInfo->book[coderInfo->bandcnt++] = HCB_ZERO;
          continue;
      }

      if ((bandqual[sb] * q_mult / gsize) < pnsthr)
      {
          coderInfo->book[coderInfo->bandcnt] = HCB_PNS;
          coderInfo->sf[coderInfo->bandcnt++] = FAAC_LRINT(FAAC_LOG10(etot + 1e-15f) * (0.5 * sfstep));
          continue;
      }

      /* sfac calculation (libfaac legacy convention):
       * sf = 100 - sfac
       * sfac = log10(rms / target_rms_error) * sfstep
       */
      faac_real enrg_line = etot / (faac_real)n_lines;
      int sfac_val = FAAC_LRINT(0.5 * FAAC_LOG10(enrg_line / (thr_line + 1e-15f)) * sfstep);

      coderInfo->book[coderInfo->bandcnt] = HCB_ESC; /* Placeholder */
      coderInfo->sf[coderInfo->bandcnt++] = 100 - sfac_val;
    }
}

int BlocQuant(CoderInfo * __restrict coder, faac_real * __restrict xr, AACQuantCfg *aacquantCfg, GlobalPsyInfo *gpsyInfo)
{
    faac_real bandlvl_all[MAX_SCFAC_BANDS];
    faac_real bandenrg_all[MAX_SCFAC_BANDS];
    faac_real bandmax_all[MAX_SCFAC_BANDS];
    int cnt, sb;
    faac_real *gxr;

#if !defined(__clang__) && defined(__GNUC__) && (GCC_VERSION >= 40600)
    static const faac_real sfstep = 1.0 / FAAC_LOG10(FAAC_SQRT(FAAC_SQRT(2.0)));
#else
    static const faac_real sfstep = 20 / 1.50515;
#endif

    coder->global_gain = 0;
    coder->bandcnt = 0;
    coder->datacnt = 0;
    coder->pe = 0.0;

    /* Pass 1: suggest scale factors */
    gxr = xr;
    for (cnt = 0; cnt < coder->groups.n; cnt++)
    {
        FaacComputeMaskingThresholds(coder, gxr, &bandlvl_all[coder->bandcnt], &bandenrg_all[coder->bandcnt], &bandmax_all[coder->bandcnt], cnt,
                (faac_real)aacquantCfg->quality/DEFQUAL, gpsyInfo);
        qlevel(coder, gxr, &bandlvl_all[coder->bandcnt], &bandenrg_all[coder->bandcnt], &bandmax_all[coder->bandcnt], cnt, (faac_real)aacquantCfg->quality, aacquantCfg->pnslevel);
        gxr += coder->groups.len[cnt] * BLOCK_LEN_SHORT;
    }

    /* Pass 2: Global bitstream compliance */
    int total_bands = coder->bandcnt;
    coder->global_gain = 100;
    for (cnt = 0; cnt < total_bands; cnt++) {
        int book = coder->book[cnt];
        if (book >= 1 && book <= HCB_ESC) {
            coder->global_gain = coder->sf[cnt];
            break;
        }
    }

    int lastsf = coder->global_gain;
    int lastis = 0;
    int lastpns = coder->global_gain - 90;
    int initpns = 1;

    for (cnt = 0; cnt < total_bands; cnt++) {
        int book = coder->book[cnt];
        int sf = coder->sf[cnt];
        if (book == HCB_INTENSITY || book == HCB_INTENSITY2) {
            sf = ClampDiff(sf, lastis);
            if (sf < 0) sf = 0;
            if (sf > 255) sf = 255;
            lastis = sf;
        } else if (book == HCB_PNS) {
            if (initpns) {
                if (sf < 0) sf = 0;
                if (sf > 255) sf = 255;
                lastpns = sf;
                initpns = 0;
            } else {
                sf = ClampDiff(sf, lastpns);
                if (sf < 0) sf = 0;
                if (sf > 255) sf = 255;
                lastpns = sf;
            }
        } else if (book >= 1 && book <= HCB_ESC) {
            /* Huffman Limit: sfacfix < 165080 / bandmax
               sfacfix = 10^((100-sf)/sfstep) => sf > 100 - sfstep * log10(165080 / bandmax)
            */
            faac_real sf_limit = 100.0 - sfstep * FAAC_LOG10(165080.0f / (bandmax_all[cnt] + 1e-15f));
            if (sf < (int)ceil(sf_limit)) sf = (int)ceil(sf_limit);

            sf = ClampDiff(sf, lastsf);
            if (sf < 0) sf = 0;
            if (sf > 255) sf = 255;
            lastsf = sf;
        } else if (book == HCB_ZERO) {
            sf = lastsf;
        }
        /* Crucial: if book == HCB_NONE (16), we leave sf alone (it's the pan value for right channel) */
        if (book != HCB_NONE) coder->sf[cnt] = sf;
    }

    /* Pass 3: Actual Quantization and PE measurement */
    gxr = xr;
    int current_band = 0;
    coder->bandcnt = 0;
    coder->datacnt = 0;
    faac_real q_mult = (faac_real)aacquantCfg->quality / 100.0f;
    faac_real current_band_lvl[NSFB_LONG];

    for (cnt = 0; cnt < coder->groups.n; cnt++) {
        int gsize = coder->groups.len[cnt];
        FaacComputeMaskingThresholds(coder, gxr, current_band_lvl, NULL, NULL, cnt,
                (faac_real)aacquantCfg->quality/DEFQUAL, gpsyInfo);

        for (sb = 0; sb < coder->sfbn; sb++) {
            int book = coder->book[current_band];
            int sf = coder->sf[current_band];
            int start = coder->sfb_offset[sb];
            int end = coder->sfb_offset[sb+1];
            int n_lines = end - start;

            coder->bandcnt = current_band;

            if (book >= 1 && book <= HCB_ESC) {
                int xitab[FRAME_LEN + 4];
                faac_real sfacfix;
                memset(xitab, 0, sizeof(xitab));

                if ((100 - sf) < -155)
                    sfacfix = 0.0;
                else
                    sfacfix = FAAC_POW(10, (faac_real)(100 - sf) / sfstep);

                int *xi = xitab;
                int win;
                for (win = 0; win < gsize; win++) {
                    qfunc(gxr + win * BLOCK_LEN_SHORT + start, xi, n_lines, sfacfix);
                    xi += n_lines;
                }
                huffbook(coder, xitab, gsize * n_lines);

                /* Update PE based on actual quantized SNR */
                faac_real thr_line = (current_band_lvl[sb] * q_mult) / (faac_real)(gsize * n_lines);
                faac_real enrg_line = bandenrg_all[current_band] / (gsize * (faac_real)n_lines);
                if (enrg_line > thr_line) {
                    coder->pe += (faac_real)n_lines * gsize * FAAC_LOG10(enrg_line / (thr_line + 1e-15f)) / FAAC_LOG10(2.0);
                }
            } else if (book == HCB_INTENSITY || book == HCB_INTENSITY2) {
                /* We must re-run huffbook for intensity stereo to ensure correct bitstream count */
                int xitab[FRAME_LEN + 4];
                memset(xitab, 0, sizeof(xitab));
                huffbook(coder, xitab, gsize * n_lines);
                coder->book[current_band] = book;
            }
            current_band++;
        }
        gxr += gsize * BLOCK_LEN_SHORT;
    }
    coder->bandcnt = total_bands;

    return 1;
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
