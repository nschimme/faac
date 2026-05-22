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

#include <limits.h>
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
static faac_real sfstep;
static faac_real max_quant_limit;

/* Sentinel: delta chain has no previous band yet (first active regular band). */
#define SF_CHAIN_UNSET INT_MIN

void QuantizeInit(void)
{
#if defined(HAVE_SSE2)
    CPUCaps caps = get_cpu_caps();
    if (caps & CPU_CAP_SSE2)
        qfunc = quantize_sse2;
    else
#endif
        qfunc = quantize_scalar;

    /* 2^0.25 (1.50515 dB) step from AAC specs */
    sfstep = 1.0 / FAAC_LOG10(FAAC_SQRT(FAAC_SQRT(2.0)));

    /* Inverse-quantizer headroom: ensures (x * gain)^0.75 + 0.4054 <= 8191.
     * Pre-calculated to avoid redundant runtime power functions. */
    max_quant_limit = FAAC_POW((faac_real)MAX_HUFF_ESC_VAL + 1.0 - MAGIC_NUMBER, 4.0/3.0);
}

/* Compute gain from integer sfac, clamping against Huffman overflow.
 * Updates *sfac if clamping was applied. Returns the usable gain. */
static faac_real gain_with_overflow_clamp(int *sfac, faac_real band_peak)
{
    faac_real gain = FAAC_POW(10, *sfac / sfstep);
    if (band_peak > 0.0 && gain * band_peak > max_quant_limit)
    {
        gain = max_quant_limit / band_peak;
        *sfac = (int)FAAC_FLOOR(FAAC_LOG10(gain) * sfstep);
        gain = FAAC_POW(10, *sfac / sfstep);
    }
    return gain;
}

/* Psychoacoustic masking thresholds and factors.
 * These values were tuned via extensive feature sweeps to maximize MOS across
 * VoIP, VSS, and Music scenarios while ensuring bitrate stability.
 * See TNS_TUNING.md for derivation details. */
#define NOISEFLOOR 0.10

#define NOISETONE         0.2    /* Weight of average energy (noise-like) in masking target */
#define TONEMASK          0.45   /* Weight of peak energy (tone-like) in masking target */
#define SHORT_PENALTY     0.45   /* Tightens masking target for short-window blocks to improve transients */
#define SHORT_FLOOR_MULT  1.5    /* Loosens noise floor for short-window blocks */

/* Floor the band/frame energy ratio so target doesn't collapse on quiet upper bands.
 * AVGE_FLOOR_FACTOR: 10^( -40 dB / 10 ) = 0.0001
 * MAXE_FLOOR_FACTOR: 10^( -30 dB / 10 ) = 0.0010 */
#define AVGE_FLOOR_FACTOR 0.0001
#define MAXE_FLOOR_FACTOR 0.0010

static faac_real compute_masking_target(faac_real avge, faac_real maxe, faac_real avgenrg,
                                        faac_real powm, int start, int end, int last,
                                        faac_real penalty)
{
    faac_real target;

    /* avgenrg is guaranteed to be positive by the totenrg gate in bmask(). */
    target = NOISETONE * FAAC_POW(avge / avgenrg, powm);
    target += (1.0 - NOISETONE) * TONEMASK * FAAC_POW(maxe / avgenrg, powm);

    target *= penalty;

    target *= 10.0 / (1.0 + ((faac_real)(start + end) / last));

    return target;
}

// band sound masking
static void bmask(CoderInfo * __restrict coderInfo, faac_real * __restrict xr0, faac_real * __restrict bandqual,
                  faac_real * __restrict bandenrg, faac_real * __restrict bandmaxe, int gnum, faac_real quality)
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

  last = (coderInfo->block_type == ONLY_SHORT_WINDOW) ? BLOCK_LEN_SHORT : BLOCK_LEN_LONG;

  for (sfb = 0; sfb < coderInfo->sfbn; sfb++)
  {
    faac_real avge, maxe;
    faac_real target;
    faac_real penalty = (coderInfo->block_type == ONLY_SHORT_WINDOW) ? SHORT_PENALTY : 1.0;
    faac_real floor_penalty = (coderInfo->block_type == ONLY_SHORT_WINDOW) ? SHORT_FLOOR_MULT : 1.0;

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
    /* Track peak magnitude to identify potential Huffman overflows. */
    bandmaxe[sfb] = FAAC_SQRT(maxe);
    maxe *= gsize;

    avgenrg = totenrg / last;
    avgenrg *= end - start;

    target = compute_masking_target(avge, maxe, avgenrg, powm, start, end, last, penalty);

    /* Floor the band/frame energy ratio so target doesn't collapse on quiet
     * upper bands. Without this, target ends up ~5 decades below rmsx at
     * 24 kHz internal SR (HE-AAC), and the magic-offset rounding in
     * quantize_scalar truncates the entire band to zeros even though sf_rel
     * never trips SF_MIN. Floor at -40 dB (avge) / -30 dB (maxe) below
     * avgenrg keeps the band alive at coarse precision. */
    {
        faac_real avge_floor = avgenrg * (faac_real)AVGE_FLOOR_FACTOR;
        faac_real avge_eff = avge > avge_floor ? avge : avge_floor;
        faac_real maxe_floor = avgenrg * (faac_real)MAXE_FLOOR_FACTOR;
        faac_real maxe_eff = maxe > maxe_floor ? maxe : maxe_floor;
        faac_real target_floor = compute_masking_target(avge_eff, maxe_eff, avgenrg, powm,
                                                        start, end, last, floor_penalty);
        if (target < target_floor) target = target_floor;
    }

    bandqual[sfb] = target * quality;
  }
}

enum {MAXSHORTBAND = 36};
// use band quality levels to quantize a group of windows
static void qlevel(CoderInfo * __restrict coderInfo,
                   const faac_real * __restrict xr0,
                   const faac_real * __restrict bandqual,
                   const faac_real * __restrict bandenrg,
                   const faac_real * __restrict bandmaxe,
                   int gnum,
                   int pnslevel,
                   int *p_last_abs  /* previous active band's absolute stored scalefactor */
                  )
{
    int sb;
    int gsize = coderInfo->groups.len[gnum];
    faac_real pnsthr = 0.1 * pnslevel;

    for (sb = 0; sb < coderInfo->sfbn && coderInfo->bandcnt < MAX_SCFAC_BANDS; sb++)
    {
      faac_real sfacfix;
      int sfac;
      int sf_rel;   /* relative scalefactor index: SF_OFFSET - sfac */
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

      etot = bandenrg[sb] / (faac_real)gsize;
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
      sf_rel = SF_OFFSET - sfac;

      /* sf_bias: IS intensity stereo energy offset pre-loaded into sf[] by
       * AACstereo() before qlevel() runs; zero for regular bands.
       * sf_abs = sf_bias + sf_rel is the value written to the bitstream.
       * Delta chain comparisons must use sf_abs, not sf_rel alone. */
      int sf_bias = coderInfo->sf[coderInfo->bandcnt];
      int sf_abs  = sf_bias + sf_rel;

      if (sf_rel < SF_MIN)
      {
          sfacfix = 0.0;
      }
      else
      {
          /* Compute gain and clamp against Huffman overflow. */
          sfacfix = gain_with_overflow_clamp(&sfac, bandmaxe[sb]);
          sf_rel = SF_OFFSET - sfac;
          sf_abs = sf_bias + sf_rel;

          /* Pre-clamp: enforce delta limits against the previous band's stored
           * value so encoder and decoder use the same gain (sf_abs). */
          if (*p_last_abs != SF_CHAIN_UNSET)
          {
              int diff         = sf_abs - *p_last_abs;
              int clamped_diff = clamp_sf_diff(diff);
              if (clamped_diff != diff)
              {
                  sf_abs = *p_last_abs + clamped_diff;
                  sf_rel = sf_abs - sf_bias;
                  sfac   = SF_OFFSET - sf_rel;
                  if (clamped_diff > 0)
                  {
                      /* Upward clamp raised gain; re-check Huffman overflow. */
                      sfacfix = gain_with_overflow_clamp(&sfac, bandmaxe[sb]);
                      sf_rel = SF_OFFSET - sfac;
                      sf_abs = sf_bias + sf_rel;
                  }
                  else
                  {
                      /* Downward clamp lowered gain; overflow impossible. */
                      sfacfix = FAAC_POW(10, sfac / sfstep);
                  }
              }
          }
      }

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
      /* Track sf_abs (full bitstream value) for the next band's delta check.
       * HCB_ZERO bands don't participate in the regular-band delta chain. */
      if (coderInfo->book[coderInfo->bandcnt] != HCB_ZERO)
          *p_last_abs = sf_abs;
      coderInfo->sf[coderInfo->bandcnt++] += sf_rel;
    }
}

int BlocQuant(CoderInfo * __restrict coder, faac_real * __restrict xr, AACQuantCfg *aacquantCfg)
{
    faac_real bandlvl[MAX_SCFAC_BANDS];
    faac_real bandenrg[MAX_SCFAC_BANDS];
    faac_real bandmaxe[MAX_SCFAC_BANDS];
    int cnt;
    faac_real *gxr;

    coder->global_gain = 0;

    coder->bandcnt = 0;
    coder->datacnt = 0;

    int lastsf = SF_CHAIN_UNSET;  /* no previous band yet; first active band skips delta clamp */

    gxr = xr;
    for (cnt = 0; cnt < coder->groups.n; cnt++)
    {
        bmask(coder, gxr, bandlvl, bandenrg, bandmaxe, cnt,
              (faac_real)aacquantCfg->quality/DEFQUAL);
        qlevel(coder, gxr, bandlvl, bandenrg, bandmaxe, cnt,
               aacquantCfg->pnslevel, &lastsf);
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

    int lastis  = 0;
    int lastpns = coder->global_gain - SF_PNS_OFFSET;
    for (cnt = 0; cnt < coder->bandcnt; cnt++)
    {
        int book = coder->book[cnt];
        if ((book == HCB_INTENSITY) || (book == HCB_INTENSITY2))
        {
            int diff = coder->sf[cnt] - lastis;
            diff = clamp_sf_diff(diff);
            lastis += diff;
            coder->sf[cnt] = lastis;
        }
        else if (book == HCB_PNS)
        {
            int diff = coder->sf[cnt] - lastpns;
            diff = clamp_sf_diff(diff);
            lastpns += diff;
            coder->sf[cnt] = lastpns;
        }
    }
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
