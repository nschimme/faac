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
    along with this program.  See <http://www.gnu.org/licenses/>.
****************************************************************************/

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "quantize.h"
#include "huff2.h"
#include "cpu_compute.h"
#include "util.h"

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

        if (tmp > 8191.0) tmp = 8191.0;
        int q = (int)(tmp + magic);
        if (q > 8191) q = 8191;
        if (q < 0) q = 0;
        xi[cnt] = (val < 0) ? -q : q;
    }
}

static QuantizeFunc qfunc = quantize_scalar;

static faac_real q43_table[8192];
#define SF_STEP 13.287712379549449
static faac_real sfacfix_tab[256];
static faac_real sfacfix075_tab[256];
static faac_real inv_sfacfix_tab[256];
static int quant_init_done = 0;

void QuantizeInit(void)
{
    if (!quant_init_done)
    {
        int i;
        for (i = 0; i < 8192; i++)
            q43_table[i] = FAAC_POW((faac_real)i, 4.0/3.0);

        for (i = 0; i < 256; i++) {
            int sfac = i - 155;
            faac_real s = FAAC_POW(10.0, (faac_real)sfac / SF_STEP);
            sfacfix_tab[i] = s;
            sfacfix075_tab[i] = FAAC_POW(s, 0.75);
            inv_sfacfix_tab[i] = (s > 1e-15) ? (1.0 / s) : 0.0;
        }

        quant_init_done = 1;
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
static void bmask(CoderInfo * __restrict coderInfo, faac_real * __restrict xr0, faac_real * __restrict bandlvl,
                  faac_real * __restrict bandenrg, faac_real * __restrict bandmax, int gnum, faac_real quality)
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
          bandlvl[sfb] = 0.0;
          bandenrg[sfb] = 0.0;
          bandmax[sfb] = 0.0;
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
    bandmax[sfb] = FAAC_SQRT(maxe);
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

    bandlvl[sfb] = target * quality;
  }
}

static faac_real compute_band_noise(const faac_real *xabs_group, const faac_real *x075_group, int start, int n, int gsize, int sfac_idx)
{
    faac_real noise = 0.0;
    int win, i;
    const faac_real magic = (faac_real)MAGIC_NUMBER;
    faac_real sfacfix075 = sfacfix075_tab[sfac_idx];
    faac_real inv_s = inv_sfacfix_tab[sfac_idx];

    for (win = 0; win < gsize; win++)
    {
        const faac_real *xa = xabs_group + win * BLOCK_LEN_SHORT + start;
        const faac_real *x75 = x075_group + win * BLOCK_LEN_SHORT + start;
        for (i = 0; i < n; i++)
        {
            faac_real val = x75[i] * sfacfix075;
            if (val > 8191.0) val = 8191.0;
            int q = (int)(val + magic);
            if (q > 8191) q = 8191;
            else if (q < 0) q = 0;
            faac_real xhat = q43_table[q] * inv_s;
            faac_real err = xa[i] - xhat;
            noise += err * err;
        }
    }
    return noise;
}

static int count_band_bits(CoderInfo *coderInfo, const faac_real *xr_group, int start, int n, int gsize, int sfac_idx)
{
    int win;
    faac_real sfacfix = sfacfix_tab[sfac_idx];

    if (sfacfix <= 1e-10)
        return 0;

    int *xi = coderInfo->xitab;
    for (win = 0; win < gsize; win++)
    {
        const faac_real *xr = xr_group + win * BLOCK_LEN_SHORT + start;
        qfunc(xr, xi, n, sfacfix);
        xi += n;
    }

    return huff_count_bits(coderInfo->xitab, gsize * n, NULL);
}

static int count_group_bits(CoderInfo *coderInfo,
                            const faac_real *xr_group,
                            int *sfac,
                            int sfac_offset,
                            int initial_bandcnt,
                            int gnum)
{
    int sb;
    int bits = 0;
    int last_book = -1;
    int section_len = 0;
    int gsize = coderInfo->groups.len[gnum];
    int max_section_len = (coderInfo->block_type == ONLY_SHORT_WINDOW ? 7 : 31);
    int section_bits = (coderInfo->block_type == ONLY_SHORT_WINDOW ? 3 : 5);

    int current_book[NSFB_LONG];
    int current_s_bits[NSFB_LONG];

    for (sb = 0; sb < coderInfo->sfbn; sb++)
    {
        int bandcnt = initial_bandcnt + sb;
        int book = coderInfo->book[bandcnt];
        int s_bits = 0;

        if (book == HCB_PNS) {
            s_bits = 0;
            bits += 9;
        } else if (book == HCB_INTENSITY || book == HCB_INTENSITY2) {
            s_bits = 0;
            bits += 7;
        } else {
            int effective_sfac = sfac[sb] + sfac_offset;
            if (effective_sfac < -155) effective_sfac = -155;
            if (effective_sfac > 100) effective_sfac = 100;

            int cache_idx = effective_sfac + 155;
            s_bits = (int)coderInfo->quantCache[bandcnt].bits[cache_idx];

            if (s_bits < 0) {
                s_bits = count_band_bits(coderInfo, xr_group, coderInfo->sfb_offset[sb],
                                        coderInfo->sfb_offset[sb+1] - coderInfo->sfb_offset[sb],
                                        gsize, cache_idx);
                coderInfo->quantCache[bandcnt].bits[cache_idx] = (short)s_bits;
            }

            if (s_bits > 0) {
                bits += 7; // Scalefactor
                book = HCB_ESC;
            } else {
                book = HCB_ZERO;
            }
        }
        current_book[sb] = book;
        current_s_bits[sb] = s_bits;
    }

    for (sb = 0; sb < coderInfo->sfbn; sb++)
    {
        int book = current_book[sb];
        if (book != last_book || section_len >= max_section_len) {
            bits += 4 + section_bits; // New section header
            last_book = book;
            section_len = 1;
        } else {
            section_len++;
        }
        bits += current_s_bits[sb];
    }
    return bits;
}

#define QUANT_UPPER_BOUND 165000.0f
static void twoloop_quant(CoderInfo *coderInfo,
                          const faac_real *xr_group,
                          const faac_real *xabs_group,
                          const faac_real *x075_group,
                          const faac_real *bandlvl,
                          const faac_real *bandenrg,
                          const faac_real *bandmax,
                          int target_bits,
                          int pnslevel,
                          int initial_bandcnt,
                          int gnum
                         )
{
    int sb, iter;
    int sfac[NSFB_LONG];
    int sfac_max[NSFB_LONG];

    int gsize = coderInfo->groups.len[gnum];
    faac_real pnsthr = 0.1 * pnslevel;

    // Step 1: Initial sfac and sfac_max
    for (sb = 0; sb < coderInfo->sfbn; sb++)
    {
        int bandcnt = initial_bandcnt + sb;
        if (coderInfo->book[bandcnt] != HCB_NONE)
        {
            sfac[sb] = 0;
            sfac_max[sb] = 100;
            continue;
        }

        int start = coderInfo->sfb_offset[sb];
        int end = coderInfo->sfb_offset[sb+1];
        int n = end - start;
        faac_real etot = bandenrg[sb] / (faac_real)gsize;
        faac_real max_xr = bandmax[sb];

        sfac_max[sb] = FAAC_LRINT(FAAC_LOG10(QUANT_UPPER_BOUND / (max_xr + 1e-10)) * SF_STEP);
        if (sfac_max[sb] > 100) sfac_max[sb] = 100;

        faac_real rmsx = FAAC_SQRT(etot / (n + 1e-10));

        if ((rmsx < NOISEFLOOR) || (!bandlvl[sb]))
        {
            coderInfo->book[bandcnt] = HCB_ZERO;
            sfac[sb] = -155;
            continue;
        }

        if (bandlvl[sb] < pnsthr)
        {
            coderInfo->book[bandcnt] = HCB_PNS;
            coderInfo->sf[bandcnt] = FAAC_LRINT(FAAC_LOG10(etot + 1e-10) * (0.5 * SF_STEP));
            sfac[sb] = -155;
            continue;
        }

        sfac[sb] = FAAC_LRINT(FAAC_LOG10(rmsx / (bandlvl[sb] + 1e-10)) * SF_STEP);
        if (sfac[sb] > sfac_max[sb]) sfac[sb] = sfac_max[sb];
        if (sfac[sb] < -155) sfac[sb] = -155;
    }

    int target_group_bits = 0;
    if (target_bits > 0)
    {
        target_group_bits = target_bits * gsize / (coderInfo->block_type == ONLY_SHORT_WINDOW ? 8 : 1);
        target_group_bits = target_group_bits * 95 / 100;
    }

    // Step 2 & 3: Loops
    for (iter = 0; iter < 4; iter++)
    {
        int changed = 0;

        // Inner loop: Global bit control
        if (target_group_bits > 0)
        {
            int low = -100, high = 100;
            for (int ii = 0; ii < 8; ii++) {
                int mid = (low + high) / 2;
                int bits = count_group_bits(coderInfo, xr_group, sfac, mid, initial_bandcnt, gnum);
                if (bits > target_group_bits) high = mid;
                else low = mid;
            }
            if (low != 0) {
                for (sb = 0; sb < coderInfo->sfbn; sb++) {
                    if (coderInfo->book[initial_bandcnt + sb] == HCB_NONE) {
                        sfac[sb] += low;
                        if (sfac[sb] > sfac_max[sb]) sfac[sb] = sfac_max[sb];
                        if (sfac[sb] < -155) sfac[sb] = -155;
                    }
                }
                changed = 1;
            }
        }

        // Outer loop: NMR refinement
        for (sb = 0; sb < coderInfo->sfbn; sb++)
        {
            int bandcnt = initial_bandcnt + sb;
            if (coderInfo->book[bandcnt] != HCB_NONE) continue;

            int current_sfac = sfac[sb];
            int cache_idx = current_sfac + 155;
            float noise = coderInfo->quantCache[bandcnt].noise[cache_idx];

            if (noise < 0.0f) {
                int start = coderInfo->sfb_offset[sb];
                int n = coderInfo->sfb_offset[sb+1] - start;
                noise = (float)compute_band_noise(xabs_group, x075_group, start, n, gsize, cache_idx);
                coderInfo->quantCache[bandcnt].noise[cache_idx] = noise;
            }

            int n = coderInfo->sfb_offset[sb+1] - coderInfo->sfb_offset[sb];
            faac_real mask = bandlvl[sb] * bandlvl[sb] * (faac_real)n * (faac_real)gsize;

            if ((faac_real)noise > 1.05 * mask)
            {
                if (sfac[sb] < sfac_max[sb]) {
                    sfac[sb]++;
                    changed = 1;
                }
            }
        }

        if (!changed) break;
    }

    // Step 4: Staging
    for (sb = 0; sb < coderInfo->sfbn; sb++)
    {
        int bandcnt = initial_bandcnt + sb;
        if (coderInfo->book[bandcnt] != HCB_NONE)
        {
            continue;
        }

        int start = coderInfo->sfb_offset[sb];
        int end = coderInfo->sfb_offset[sb+1];
        int n = end - start;
        faac_real sfacfix = sfacfix_tab[sfac[sb] + 155];

        int saved_datacnt = coderInfo->datacnt;

        if (sfacfix <= 1e-10)
        {
            memset(coderInfo->xitab, 0, gsize * n * sizeof(int));
        }
        else
        {
            int *xi = coderInfo->xitab;
            for (int win = 0; win < gsize; win++)
            {
                const faac_real *xr = xr_group + win * BLOCK_LEN_SHORT + start;
                qfunc(xr, xi, n, sfacfix);
                xi += n;
            }
        }
        int old_bandcnt = coderInfo->bandcnt;
        coderInfo->bandcnt = bandcnt;
        int res = huffbook(coderInfo, coderInfo->xitab, gsize * n);
        // Robust fallback
        if (res < 0) {
             coderInfo->datacnt = saved_datacnt; // Rollback
             sfac[sb] -= 8;
             if (sfac[sb] < -155) sfac[sb] = -155;
             sfacfix = sfacfix_tab[sfac[sb] + 155];
             int *xi = coderInfo->xitab;
             for (int win = 0; win < gsize; win++) {
                 const faac_real *xr = xr_group + win * BLOCK_LEN_SHORT + start;
                 qfunc(xr, xi, n, sfacfix);
                 xi += n;
             }
             if (huffbook(coderInfo, coderInfo->xitab, gsize * n) < 0) {
                 coderInfo->datacnt = saved_datacnt; // Rollback again
                 coderInfo->book[bandcnt] = HCB_ZERO;
             }
        }
        coderInfo->bandcnt = old_bandcnt;
        coderInfo->sf[bandcnt] = SF_OFFSET - sfac[sb];
    }
}

int BlocQuant(CoderInfo * __restrict coder, faac_real * __restrict xr, AACQuantCfg *aacquantCfg)
{
    faac_real bandlvl[MAX_SCFAC_BANDS];
    faac_real bandenrg[MAX_SCFAC_BANDS];
    faac_real bandmax[MAX_SCFAC_BANDS];
    int cnt, i;
    faac_real *gxr;

    memset(coder->xabs, 0, sizeof(coder->xabs));
    memset(coder->x075, 0, sizeof(coder->x075));

    for (i = 0; i < 1024; i++) {
        faac_real a = FAAC_FABS(xr[i]);
        coder->xabs[i] = a;
        coder->x075[i] = FAAC_SQRT(a * FAAC_SQRT(a));
    }

    coder->global_gain = 0;
    coder->bandcnt = 0;
    coder->datacnt = 0;

    int total_bands = coder->sfbn * coder->groups.n;
    if (total_bands > MAX_SCFAC_BANDS) total_bands = MAX_SCFAC_BANDS;

    for (i = 0; i < total_bands; i++) {
        for (int j = 0; j < 256; j++) {
            coder->quantCache[i].bits[j] = -1;
            coder->quantCache[i].noise[j] = -1.0f;
        }
    }

    for (i = 0; i < MAX_SCFAC_BANDS; i++) {
        if (coder->book[i] != HCB_INTENSITY && coder->book[i] != HCB_INTENSITY2)
            coder->book[i] = HCB_NONE;
    }

    {
        int initial_bandcnt = 0;

        gxr = xr;
        for (cnt = 0; cnt < coder->groups.n; cnt++)
        {
            int gsize = coder->groups.len[cnt];
            int offset = (int)(gxr - xr);
            bmask(coder, gxr, bandlvl, bandenrg, bandmax, cnt,
                  (faac_real)aacquantCfg->quality/DEFQUAL);

            twoloop_quant(coder, gxr, coder->xabs + offset, coder->x075 + offset, bandlvl, bandenrg, bandmax,
                         aacquantCfg->target_bits, aacquantCfg->pnslevel, initial_bandcnt, cnt);

            gxr += gsize * BLOCK_LEN_SHORT;
            initial_bandcnt += coder->sfbn;
        }
        coder->bandcnt = initial_bandcnt;

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

        int lastsf = coder->global_gain;
        int lastis = 0;
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
