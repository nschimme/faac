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
    along with this program.  If not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
****************************************************************************/

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "quantize.h"
#include "huff2.h"
#include "cpu_compute.h"

#ifdef __GNUC__
#define GCC_VERSION (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)
#endif

typedef void (*QuantizeFunc)(const faac_real *xr, int *xi, int n, faac_real sfacfix);
#if defined(HAVE_SSE2)
extern void quantize_sse2(const faac_real *xr, int *xi, int n, faac_real sfacfix);
#endif

static void quantize_scalar(const faac_real *xr, int *xi, int n, faac_real sfacfix)
{
    const faac_real magic = MAGIC_NUMBER;
    int cnt;
    for (cnt = 0; cnt < n; cnt++) {
        faac_real val = xr[cnt];
        faac_real tmp = FAAC_FABS(val) * sfacfix;
        tmp = FAAC_SQRT(tmp * FAAC_SQRT(tmp));
        int q = (int)(tmp + magic);
        xi[cnt] = (val < 0) ? -q : q;
    }
}

static QuantizeFunc qfunc = quantize_scalar;
static faac_real sfstep;
static faac_real max_quant_limit;
#define SF_CHAIN_UNSET INT_MIN

void QuantizeInit(void) {
#if defined(HAVE_SSE2)
    CPUCaps caps = get_cpu_caps();
    if (caps & CPU_CAP_SSE2) qfunc = quantize_sse2; else qfunc = quantize_scalar;
#else
    qfunc = quantize_scalar;
#endif
    sfstep = 1.0 / FAAC_LOG10(FAAC_SQRT(FAAC_SQRT(2.0)));
    max_quant_limit = FAAC_POW((faac_real)MAX_HUFF_ESC_VAL + 1.0 - MAGIC_NUMBER, 4.0/3.0);
}

static faac_real gain_with_overflow_clamp(int *sfac, faac_real band_peak) {
    faac_real gain = FAAC_POW(10, *sfac / sfstep);
    if (band_peak > 0.0 && gain * band_peak > max_quant_limit) {
        gain = max_quant_limit / band_peak;
        *sfac = (int)FAAC_FLOOR(FAAC_LOG10(gain) * sfstep);
        gain = FAAC_POW(10, *sfac / sfstep);
    }
    return gain;
}

#define NOISEFLOOR 0.4
#define NOISETONE 0.2
#define TONEMASK 0.45
#define SHORT_PENALTY 0.45
#define AVGE_FLOOR_FACTOR 0.0010
#define MAXE_FLOOR_FACTOR 0.0050

static void bmask(CoderInfo *coderInfo, faac_real *xr0, faac_real *bandqual,
                  faac_real *bandenrg, faac_real *bandmaxe, int gnum, faac_real quality)
{
  int sfb, start, end, cnt, win, last;
  int *cb_offset = coderInfo->sfb_offset;
  faac_real avgenrg, powm = 0.4, totenrg = 0.0;
  int gsize = coderInfo->groups.len[gnum];
  int total_len = coderInfo->sfb_offset[coderInfo->sfbn];
  for (win = 0; win < gsize; win++) {
      faac_real *xr = xr0 + win * BLOCK_LEN_SHORT;
      for (cnt = 0; cnt < total_len; cnt++) totenrg += xr[cnt] * xr[cnt];
  }
  if (totenrg < ((NOISEFLOOR * NOISEFLOOR) * (faac_real)(gsize * total_len))) {
      for (sfb = 0; sfb < coderInfo->sfbn; sfb++) { bandqual[sfb] = 0.0; bandenrg[sfb] = 0.0; }
      return;
  }
  last = (coderInfo->block_type == ONLY_SHORT_WINDOW) ? BLOCK_LEN_SHORT : BLOCK_LEN_LONG;
  for (sfb = 0; sfb < coderInfo->sfbn; sfb++) {
    faac_real avge = 0.0, maxe = 0.0, target;
    start = cb_offset[sfb]; end = cb_offset[sfb + 1];
    int n = end - start;
    for (win = 0; win < gsize; win++) {
        faac_real *xr = xr0 + win * BLOCK_LEN_SHORT + start;
        for (cnt = 0; cnt < n; cnt++) {
            faac_real val = xr[cnt];
            faac_real e = val * val;
            avge += e; if (maxe < e) maxe = e;
        }
    }
    bandenrg[sfb] = avge; bandmaxe[sfb] = FAAC_SQRT(maxe);
    maxe *= gsize; avgenrg = (totenrg / last) * n;
    if (avge < avgenrg * AVGE_FLOOR_FACTOR) avge = avgenrg * AVGE_FLOOR_FACTOR;
    if (maxe < avgenrg * MAXE_FLOOR_FACTOR) maxe = avgenrg * MAXE_FLOOR_FACTOR;
    target = NOISETONE * FAAC_POW(avge/avgenrg, powm) + (1.0 - NOISETONE) * TONEMASK * FAAC_POW(maxe/avgenrg, powm);
    if (coderInfo->block_type == ONLY_SHORT_WINDOW) target *= SHORT_PENALTY;
    target *= 10.0 / (1.0 + ((faac_real)(start+end)/last));
    bandqual[sfb] = target * quality;
  }
}

static void qlevel(CoderInfo *coderInfo, const faac_real *xr0, const faac_real *bandqual,
                   const faac_real *bandenrg, const faac_real *bandmaxe, int gnum, int pnslevel, int *p_last_abs)
{
    int sb, gsize = coderInfo->groups.len[gnum];
    faac_real pnsthr = 0.1 * pnslevel;
    for (sb = 0; sb < coderInfo->sfbn && coderInfo->bandcnt < MAX_SCFAC_BANDS; sb++) {
      faac_real sfacfix; int sfac, sf_rel, start, end, win, n; int xitab[FRAME_LEN];
      if (coderInfo->book[coderInfo->bandcnt] != HCB_NONE) { coderInfo->bandcnt++; continue; }
      start = coderInfo->sfb_offset[sb]; end = coderInfo->sfb_offset[sb+1]; n = end - start;
      faac_real etot = bandenrg[sb] / (faac_real)gsize;
      faac_real rmsx = FAAC_SQRT(etot / n);
      if ((rmsx < NOISEFLOOR) || (!bandqual[sb])) { coderInfo->book[coderInfo->bandcnt++] = HCB_ZERO; continue; }
      if (bandqual[sb] < pnsthr) {
          coderInfo->book[coderInfo->bandcnt] = HCB_PNS;
          coderInfo->sf[coderInfo->bandcnt] += FAAC_LRINT(FAAC_LOG10(etot) * (0.5 * sfstep));
          coderInfo->bandcnt++; continue;
      }
      sfac = FAAC_LRINT(FAAC_LOG10(bandqual[sb] / rmsx) * sfstep);
      sf_rel = SF_OFFSET - sfac;
      int sf_bias = coderInfo->sf[coderInfo->bandcnt], sf_abs = sf_bias + sf_rel;
      if (sf_rel < SF_MIN) { sfacfix = 0.0; } else {
          sfacfix = gain_with_overflow_clamp(&sfac, bandmaxe[sb]);
          sf_rel = SF_OFFSET - sfac; sf_abs = sf_bias + sf_rel;
          if (*p_last_abs != SF_CHAIN_UNSET) {
              int diff = sf_abs - *p_last_abs, cl_diff = clamp_sf_diff(diff);
              if (cl_diff != diff) {
                  sf_abs = *p_last_abs + cl_diff; sf_rel = sf_abs - sf_bias; sfac = SF_OFFSET - sf_rel;
                  if (cl_diff > 0) sfacfix = gain_with_overflow_clamp(&sfac, bandmaxe[sb]); else sfacfix = FAAC_POW(10, sfac / sfstep);
                  sf_rel = SF_OFFSET - sfac; sf_abs = sf_bias + sf_rel;
              }
          }
          if (sf_abs < 0 || sf_abs > SF_MAX_ABS) {
              sf_abs = (sf_abs < 0) ? 0 : SF_MAX_ABS; sf_rel = sf_abs - sf_bias; sfac = SF_OFFSET - sf_rel;
              sfacfix = gain_with_overflow_clamp(&sfac, bandmaxe[sb]); sf_rel = SF_OFFSET - sfac; sf_abs = sf_bias + sf_rel;
          }
      }
      if (sfacfix <= 0.0) {
          memset(xitab, 0, gsize * n * sizeof(int)); coderInfo->book[coderInfo->bandcnt] = HCB_ZERO;
      } else {
          int *xi = xitab;
          for (win = 0; win < gsize; win++) { qfunc(xr0 + win * BLOCK_LEN_SHORT + start, xi, n, sfacfix); xi += n; }
          int prev = (coderInfo->bandcnt > 0) ? coderInfo->book[coderInfo->bandcnt-1] : HCB_NONE;
          huffbook(coderInfo, xitab, gsize * n, prev);
      }
      if (coderInfo->book[coderInfo->bandcnt] != HCB_ZERO) *p_last_abs = sf_abs;
      coderInfo->sf[coderInfo->bandcnt++] += sf_rel;
    }
}

int BlocQuant(CoderInfo *coder, faac_real *xr, AACQuantCfg *aacquantCfg) {
    faac_real bandlvl[MAX_SCFAC_BANDS], bandenrg[MAX_SCFAC_BANDS], bandmaxe[MAX_SCFAC_BANDS];
    int cnt; faac_real *gxr = xr;
    coder->global_gain = 0; coder->bandcnt = 0; coder->datacnt = 0;
    int lastsf = SF_CHAIN_UNSET;
    for (cnt = 0; cnt < coder->groups.n; cnt++) {
        bmask(coder, gxr, bandlvl, bandenrg, bandmaxe, cnt, (faac_real)aacquantCfg->quality/DEFQUAL);
        qlevel(coder, gxr, bandlvl, bandenrg, bandmaxe, cnt, aacquantCfg->pnslevel, &lastsf);
        gxr += coder->groups.len[cnt] * BLOCK_LEN_SHORT;
    }
    coder->global_gain = 0;
    for (cnt = 0; cnt < coder->bandcnt; cnt++) {
        int book = coder->book[cnt];
        if (book && book != HCB_INTENSITY && book != HCB_INTENSITY2 && book != HCB_PNS) { coder->global_gain = coder->sf[cnt]; break; }
    }
    int lastis = 0, lastpns = coder->global_gain - SF_PNS_OFFSET;
    for (cnt = 0; cnt < coder->bandcnt; cnt++) {
        int book = coder->book[cnt];
        if (book == HCB_INTENSITY || book == HCB_INTENSITY2) {
            int diff = clamp_sf_diff(coder->sf[cnt] - lastis); lastis += diff; coder->sf[cnt] = lastis;
        } else if (book == HCB_PNS) {
            int diff = clamp_sf_diff(coder->sf[cnt] - lastpns); lastpns += diff; coder->sf[cnt] = lastpns;
        }
    }
    return 1;
}

void CalcBW(unsigned *bw, int rate, SR_INFO *sr, AACQuantCfg *aacquantCfg) {
    int max = *bw * (BLOCK_LEN_SHORT << 1) / rate, cnt, l = 0;
    for (cnt = 0; cnt < sr->num_cb_short; cnt++) { if (l >= max) break; l += sr->cb_width_short[cnt]; }
    aacquantCfg->max_cbs = cnt; if (aacquantCfg->pnslevel) *bw = (faac_real)l * rate / (BLOCK_LEN_SHORT << 1);
    max = *bw * (BLOCK_LEN_LONG << 1) / rate; l = 0;
    for (cnt = 0; cnt < sr->num_cb_long; cnt++) { if (l >= max) break; l += sr->cb_width_long[cnt]; }
    aacquantCfg->max_cbl = cnt; aacquantCfg->max_l = l; *bw = (faac_real)l * rate / (BLOCK_LEN_LONG << 1);
}

enum {MINSFB = 2};
static void calce(faac_real *xr, const int *bands, faac_real e[NSFB_SHORT], int maxsfb, int maxl) {
    int sfb, l; for (l = maxl; l < bands[maxsfb]; l++) xr[l] = 0.0;
    for (sfb = MINSFB; sfb < maxsfb; sfb++) {
        e[sfb] = 0; for (l = bands[sfb]; l < bands[sfb + 1]; l++) e[sfb] += xr[l] * xr[l];
    }
}
static void resete(faac_real min[NSFB_SHORT], faac_real max[NSFB_SHORT], faac_real e[NSFB_SHORT], int maxsfb) {
    int sfb; for (sfb = MINSFB; sfb < maxsfb; sfb++) min[sfb] = max[sfb] = e[sfb];
}
void BlocGroup(faac_real *xr, CoderInfo *coderInfo, AACQuantCfg *cfg) {
    int win, sfb, win0 = 0, fastmin, maxsfb, maxl;
    faac_real e[NSFB_SHORT], min[NSFB_SHORT], max[NSFB_SHORT];
    const faac_real thr = 3.0;
    if (coderInfo->block_type != ONLY_SHORT_WINDOW) { coderInfo->groups.n = 1; coderInfo->groups.len[0] = 1; return; }
    maxl = cfg->max_l / 8; maxsfb = cfg->max_cbs; fastmin = ((maxsfb - MINSFB) * 3) >> 2;
    calce(xr, coderInfo->sfb_offset, e, maxsfb, maxl); resete(min, max, e, maxsfb);
    coderInfo->groups.n = 0;
    for (win = 1; win < MAX_SHORT_WINDOWS; win++) {
        int fast = 0; calce(xr + win * BLOCK_LEN_SHORT, coderInfo->sfb_offset, e, maxsfb, maxl);
        for (sfb = MINSFB; sfb < maxsfb; sfb++) {
            if (min[sfb] > e[sfb]) min[sfb] = e[sfb]; if (max[sfb] < e[sfb]) max[sfb] = e[sfb];
            if (max[sfb] > thr * min[sfb]) fast++;
        }
        if (fast > fastmin) { coderInfo->groups.len[coderInfo->groups.n++] = win - win0; win0 = win; resete(min, max, e, maxsfb); }
    }
    coderInfo->groups.len[coderInfo->groups.n++] = win - win0;
}
void BlocStat(void) {}
