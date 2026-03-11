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

static int quantize_and_count_bits(const faac_real *xr0, int gsize, int end, int start, faac_real sfacfix, faac_real *distortion, int *xi_out);

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
        if (q > 8191) q = 8191;
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

static void bmask(CoderInfo * __restrict coderInfo, faac_real * __restrict xr0, faac_real * __restrict bandlvl,
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
  int total_len = coderInfo->sfb_offset[coderInfo->sfbn];

  for (win = 0; win < gsize; win++)
  {
      xr = xr0 + win * BLOCK_LEN_SHORT;
      for (cnt = 0; cnt < total_len; cnt++)
      {
          totenrg += xr[cnt] * xr[cnt];
      }
  }

  if (totenrg < (1e-15 * (faac_real)gsize * total_len))
  {
      for (sfb = 0; sfb < coderInfo->sfbn; sfb++)
      {
          bandlvl[sfb] = 0.0;
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

#define NOISETONE 0.5
    if (coderInfo->block_type == ONLY_SHORT_WINDOW)
    {
        last = BLOCK_LEN_SHORT;
        avgenrg = totenrg / last;
        avgenrg *= end - start;
        target = NOISETONE * FAAC_POW(avge/(avgenrg+1e-15), powm) + (1.0 - NOISETONE) * 0.45 * FAAC_POW(maxe/(avgenrg+1e-15), powm);
        target *= 1.5;
    }
    else
    {
        last = BLOCK_LEN_LONG;
        avgenrg = totenrg / last;
        avgenrg *= end - start;
        target = NOISETONE * FAAC_POW(avge/(avgenrg+1e-15), powm) + (1.0 - NOISETONE) * 0.45 * FAAC_POW(maxe/(avgenrg+1e-15), powm);
    }

    target *= 10.0 / (1.0 + ((faac_real)(start+end)/last));
    bandlvl[sfb] = target * quality;
  }
}

static void compute_band_bit_targets(CoderInfo *coderInfo, faac_real *bandlvl, faac_real *bandenrg, int frame_target_bits, int *band_bits, int gsize)
{
    int b;
    faac_real total_difficulty = 0.0;
    faac_real difficulty[MAX_SCFAC_BANDS];
    int sfbn = coderInfo->sfbn;
    int base_band = coderInfo->bandcnt;

    int group_samples = (coderInfo->block_type == ONLY_SHORT_WINDOW) ? gsize * BLOCK_LEN_SHORT : BLOCK_LEN_LONG;
    int group_target_bits = (int)((faac_real)frame_target_bits * group_samples / FRAME_LEN);

    if (group_target_bits <= 0) {
        for (b = 0; b < sfbn; b++) band_bits[b] = 0;
        return;
    }

    for (b = 0; b < sfbn; b++) {
        faac_real threshold = bandlvl[b] * coderInfo->adj_thr[base_band + b];
        faac_real smr = bandenrg[b] / (threshold * threshold + 1e-12);
        difficulty[b] = (smr > 1.1) ? (faac_real)FAAC_LOG10(smr) : 0.05f;
        total_difficulty += difficulty[b];
    }

    if (total_difficulty <= 0) {
        for (b = 0; b < sfbn; b++) band_bits[b] = group_target_bits / sfbn;
        return;
    }

    for (b = 0; b < sfbn; b++) {
        band_bits[b] = (int)(group_target_bits * difficulty[b] / total_difficulty);
        if (band_bits[b] < gsize) band_bits[b] = gsize;
    }
}

static faac_real rd_cost(faac_real distortion, int bits, faac_real lambda, int target_bits, faac_real etot)
{
    faac_real cost = distortion + lambda * bits;
    if (target_bits > 0 && bits > target_bits) {
        /* Heuristic penalty to stay close to budget without sacrificing transparency */
        cost += (faac_real)(bits - target_bits) * lambda * 5.0f;
    }
    return cost;
}

static void qlevel(CoderInfo * __restrict coderInfo,
                   const faac_real * __restrict bandlvl,
                   const faac_real * __restrict bandenrg,
                   const faac_real * __restrict xr0,
                   int gnum,
                   int pnslevel,
                   int *band_bits,
                   faac_real *prev_scf,
                   faac_real *prev_band_energy,
                   int bitRate,
                   int *lastsf,
                   int *lastpns
                  )
{
    int sb;
    static const faac_real sfstep = 13.287712f;
    int gsize = coderInfo->groups.len[gnum];
#ifndef DRM
    faac_real pnsthr = 0.1 * pnslevel;
#endif
    faac_real bitrate_index = (faac_real)bitRate / 16000.0;
    int trial_xi_raw[7][1024 + 64];
    int *trial_xi[7] = {trial_xi_raw[0], trial_xi_raw[1], trial_xi_raw[2], trial_xi_raw[3], trial_xi_raw[4], trial_xi_raw[5], trial_xi_raw[6]};

    for (sb = 0; sb < coderInfo->sfbn; sb++)
    {
      int sfac;
      faac_real rmsx;
      faac_real etot;
      int start, end;
      faac_real quality = bandlvl[sb] * coderInfo->adj_thr[coderInfo->bandcnt];

      if (coderInfo->book[coderInfo->bandcnt] != HCB_NONE) {
          coderInfo->bandcnt++;
          continue;
      }

      start = coderInfo->sfb_offset[sb];
      end = coderInfo->sfb_offset[sb+1];
      etot = bandenrg[sb] / (faac_real)gsize;

      if (end <= start || etot < 1e-15) {
          coderInfo->book[coderInfo->bandcnt++] = HCB_ZERO;
          continue;
      }
      rmsx = FAAC_SQRT(etot / (end - start));

#ifndef DRM
      if (quality < pnsthr && quality > 0) {
          int trial_sf = 100 + FAAC_LRINT(sfstep * FAAC_LOG10(rmsx / 1e-6));
          if (trial_sf > 155) trial_sf = 155;
          if (trial_sf < 0) trial_sf = 0;

          int diff = trial_sf - *lastpns;
          if (diff < -60) diff = -60;
          if (diff > 60) diff = 60;
          *lastpns += diff;
          coderInfo->book[coderInfo->bandcnt] = HCB_PNS;
          coderInfo->sf[coderInfo->bandcnt] = *lastpns;
          coderInfo->bandcnt++;
          continue;
      }
#endif

      faac_real bps = (faac_real)band_bits[sb] / (gsize * (end - start));
      sfac = 100 + FAAC_LRINT(sfstep * FAAC_LOG10(rmsx / (bps + 0.15)));

      {
          faac_real d[7], f[7], c[7];
          int b[7], trials[7];
          int n = end - start;
          int i, best_idx = 3;
          /* Lambda calibrated for high-quality music at 64kbps/ch */
          faac_real lambda = 0.0001 * etot * FAAC_POW(2.0, -bitrate_index / 4.0);

          for (i = 0; i < 7; i++) {
              int t_sfac = sfac + (i - 3) * 2;

              if (*lastsf != -1) {
                  int diff = t_sfac - *lastsf;
                  if (diff < -60) diff = -60;
                  if (diff > 60) diff = 60;
                  trials[i] = *lastsf + diff;
              } else {
                  trials[i] = t_sfac;
              }
              if (trials[i] < 0) trials[i] = 0;
              if (trials[i] > 255) trials[i] = 255;

              f[i] = (faac_real)FAAC_POW(2.0, 0.25 * (100 - trials[i]));
              b[i] = quantize_and_count_bits(xr0, gsize, n, start, f[i], &d[i], trial_xi[i]);
              c[i] = rd_cost(d[i], b[i], lambda, band_bits[sb], etot);
              if (i == 0 || c[i] < c[best_idx]) best_idx = i;
          }
          *lastsf = trials[best_idx];
          if (best_idx != 0) memcpy(trial_xi[0], trial_xi[best_idx], gsize * n * sizeof(int));
      }

      huffbook(coderInfo, trial_xi[0], gsize * (end - start));
      coderInfo->sf[coderInfo->bandcnt] = *lastsf;
      prev_scf[coderInfo->bandcnt] = (faac_real)coderInfo->sf[coderInfo->bandcnt];
      prev_band_energy[coderInfo->bandcnt] = bandenrg[sb];
      coderInfo->bandcnt++;
    }
}

static int quantize_and_count_bits(const faac_real * xr0, int gsize, int end, int start, faac_real sfacfix, faac_real * distortion, int * xi_out)
{
    int win;
    int total_len = gsize * end;
    int * p_xi = xi_out;
    faac_real local_distortion = 0;
    int any_nonzero = 0;

    if (sfacfix <= 0.0) {
        memset(xi_out, 0, total_len * sizeof(int));
        for (win = 0; win < gsize; win++) {
            const faac_real * xr = xr0 + win * BLOCK_LEN_SHORT + start;
            int i;
            for (i = 0; i < end; i++) local_distortion += xr[i] * xr[i];
        }
        *distortion = local_distortion;
        return 0;
    }

    faac_real inv_sfacfix = 1.0f / (sfacfix + 1e-20f);
    for (win = 0; win < gsize; win++) {
        const faac_real * xr = xr0 + win * BLOCK_LEN_SHORT + start;
        qfunc(xr, p_xi, end, sfacfix);
        int i;
        for (i = 0; i < end; i++) {
            int q = (p_xi[i] < 0) ? -p_xi[i] : p_xi[i];
            faac_real q_val = (q == 0) ? 0 : (q < 1024 ? xi_pow_table[q] : (faac_real)FAAC_POW((faac_real)q, 1.33333333f));
            if (q > 0) any_nonzero = 1;
            q_val *= inv_sfacfix;
            faac_real diff = FAAC_FABS(xr[i]) - q_val;
            local_distortion += diff * diff;
        }
        p_xi += end;
    }
    *distortion = local_distortion;
    if (!any_nonzero) return 0;

    int maxq = 0;
    for (win = 0; win < total_len; win++) {
        int q = (xi_out[win] < 0) ? -xi_out[win] : xi_out[win];
        if (maxq < q) maxq = q;
    }
    int bookmin;
    if (maxq < 2) bookmin = 1; else if (maxq < 3) bookmin = 3; else if (maxq < 5) bookmin = 5;
    else if (maxq < 8) bookmin = 7; else if (maxq < 13) bookmin = 9; else bookmin = 11;

    int len = huff_count_bits(xi_out, total_len, bookmin);
    if (bookmin < 11) {
        int len2 = huff_count_bits(xi_out, total_len, bookmin + 1);
        if (len2 < len) len = len2;
    }
    return (int)(len * 1.05 + 11);
}

int BlocQuant(CoderInfo * coder, faac_real * xr, AACQuantCfg *aacquantCfg, int frame_target_bits, faac_real *prev_scf, faac_real *prev_band_energy)
{
    faac_real bandlvl[MAX_SCFAC_BANDS];
    faac_real bandenrg[MAX_SCFAC_BANDS];
    int band_bits[MAX_SCFAC_BANDS];
    int cnt;
    faac_real *gxr;
    int lastsf = -1;
    int lastpns = 100;

    coder->bandcnt = 0;
    coder->datacnt = 0;
#ifdef DRM
    coder->iLenReordSpData = 0; coder->iLenLongestCW = 0; coder->cur_cw = 0;
#endif

    gxr = xr;
    for (cnt = 0; cnt < coder->groups.n; cnt++)
    {
        bmask(coder, gxr, bandlvl, bandenrg, cnt, (faac_real)aacquantCfg->quality/DEFQUAL);
        compute_band_bit_targets(coder, bandlvl, bandenrg, frame_target_bits, band_bits, coder->groups.len[cnt]);
        qlevel(coder, bandlvl, bandenrg, gxr, cnt, aacquantCfg->pnslevel, band_bits, prev_scf, prev_band_energy, aacquantCfg->bitRate, &lastsf, &lastpns);
        gxr += coder->groups.len[cnt] * BLOCK_LEN_SHORT;
    }

    coder->global_gain = 0;
    for (cnt = 0; cnt < coder->bandcnt; cnt++) {
        int book = coder->book[cnt];
        if (book && book != HCB_INTENSITY && book != HCB_INTENSITY2 && book != HCB_PNS) {
            coder->global_gain = coder->sf[cnt];
            break;
        }
    }
    if (coder->global_gain == 0 && coder->bandcnt > 0) coder->global_gain = coder->sf[0];
    if (coder->global_gain == 0) coder->global_gain = 100;

    return 1;
}

void CalcBW(unsigned *bw, int rate, SR_INFO *sr, AACQuantCfg *aacquantCfg)
{
    int max = *bw * (BLOCK_LEN_SHORT << 1) / rate;
    int cnt, l = 0;
    for (cnt = 0; cnt < sr->num_cb_short; cnt++) { if (l >= max) break; l += sr->cb_width_short[cnt]; }
    aacquantCfg->max_cbs = cnt;
    if (aacquantCfg->pnslevel) *bw = (faac_real)l * rate / (BLOCK_LEN_SHORT << 1);

    max = *bw * (BLOCK_LEN_LONG << 1) / rate;
    l = 0;
    for (cnt = 0; cnt < sr->num_cb_long; cnt++) { if (l >= max) break; l += sr->cb_width_long[cnt]; }
    aacquantCfg->max_cbl = cnt; aacquantCfg->max_l = l;
    *bw = (faac_real)l * rate / (BLOCK_LEN_LONG << 1);
}

void BlocGroup(faac_real *xr, CoderInfo *coderInfo, AACQuantCfg *cfg)
{
    int win, sfb, win0, fastmin, maxsfb, maxl, l;
    faac_real e[NSFB_SHORT], min[NSFB_SHORT], max[NSFB_SHORT];
    const faac_real thr = 3.0;

    if (coderInfo->block_type != ONLY_SHORT_WINDOW) {
        coderInfo->groups.n = 1; coderInfo->groups.len[0] = 1; return;
    }
    maxl = cfg->max_l / 8; maxsfb = cfg->max_cbs; fastmin = ((maxsfb - 2) * 3) >> 2;
#ifdef DRM
    coderInfo->groups.n = 1; coderInfo->groups.len[0] = 8; return;
#endif

    for (l = maxl; l < coderInfo->sfb_offset[maxsfb]; l++) xr[l] = 0.0;
    for (sfb = 2; sfb < maxsfb; sfb++) {
        e[sfb] = 0;
        for (l = coderInfo->sfb_offset[sfb]; l < coderInfo->sfb_offset[sfb + 1]; l++) e[sfb] += xr[l] * xr[l];
    }

    for (sfb = 2; sfb < maxsfb; sfb++) min[sfb] = max[sfb] = e[sfb];
    win0 = 0; coderInfo->groups.n = 0;
    for (win = 1; win < MAX_SHORT_WINDOWS; win++) {
        int fast = 0;
        faac_real *xr_win = xr + win * BLOCK_LEN_SHORT;
        for (l = maxl; l < coderInfo->sfb_offset[maxsfb]; l++) xr_win[l] = 0.0;
        for (sfb = 2; sfb < maxsfb; sfb++) {
            faac_real en = 0;
            for (l = coderInfo->sfb_offset[sfb]; l < coderInfo->sfb_offset[sfb + 1]; l++) en += xr_win[l] * xr_win[l];
            if (min[sfb] > en) min[sfb] = en;
            if (max[sfb] < en) max[sfb] = en;
            if (max[sfb] > thr * min[sfb]) fast++;
        }
        if (fast > fastmin) {
            coderInfo->groups.len[coderInfo->groups.n++] = win - win0;
            win0 = win;
            for (sfb = 2; sfb < maxsfb; sfb++) {
                faac_real en = 0;
                for (l = coderInfo->sfb_offset[sfb]; l < coderInfo->sfb_offset[sfb + 1]; l++) en += xr_win[l] * xr_win[l];
                min[sfb] = max[sfb] = en;
            }
        }
    }
    coderInfo->groups.len[coderInfo->groups.n++] = win - win0;
}

void BlocStat(void) {}
