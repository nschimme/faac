/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2002 Krzysztof Nikiel
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * $Id: psychkni.c,v 1.19 2012/03/01 18:34:17 knik Exp $
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "blockswitch.h"
#include "coder.h"
#include "fft.h"
#include "util.h"
#include <faac.h>
#include "filtbank.h"
#include "quantize.h"

typedef float psyfloat;

typedef struct {
    faac_real ath_table[BLOCK_LEN_LONG];
} Psy2Global;

typedef struct
{
  /* bandwidth */
  int bandS;
  int lastband;

  /* band volumes */
  psyfloat *engPrev[8];
  psyfloat *eng[8];
  psyfloat *engNext[8];
  psyfloat *engNext2[8];
}
psydata_t;


static void Hann(GlobalPsyInfo * gpsyInfo, faac_real *inSamples, int size)
{
  int i;

  /* Applying Hann window */
  if (size == BLOCK_LEN_LONG * 2)
  {
    for (i = 0; i < size; i++)
      inSamples[i] *= gpsyInfo->hannWindow[i];
  }
  else
  {
    for (i = 0; i < size; i++)
      inSamples[i] *= gpsyInfo->hannWindowS[i];
  }
}

#define PRINTSTAT 0
#if PRINTSTAT
static struct {
    int tot;
    int s;
} frames;
#endif

static void PsyCheckShort(GlobalPsyInfo * gpsyInfo, PsyInfo * psyInfo, faac_real quality)
{
  enum {PREVS = 2, NEXTS = 2};
  psydata_t *psydata = psyInfo->data;
  int lastband = psydata->lastband;
  int firstband = 2;
  int sfb, win;
  psyfloat *lasteng;

  psyInfo->block_type = ONLY_LONG_WINDOW;

  lasteng = NULL;
  for (win = 0; win < PREVS + 8 + NEXTS; win++)
  {
      psyfloat *eng;

      if (win < PREVS)
          eng = psydata->engPrev[win + 8 - PREVS];
      else if (win < (PREVS + 8))
          eng = psydata->eng[win - PREVS];
      else
          eng = psydata->engNext[win - PREVS - 8];

      if (lasteng)
      {
          faac_real toteng = 0.0;
          faac_real volchg = 0.0;

          for (sfb = firstband; sfb < lastband; sfb++)
          {
              toteng += (eng[sfb] < lasteng[sfb]) ? eng[sfb] : lasteng[sfb];
              volchg += FAAC_FABS(eng[sfb] - lasteng[sfb]);
          }

          faac_real threshold = 3.0;

          /* Smoothly increase sensitivity for low sample rates */
          if (gpsyInfo->sampleRate < 44100)
          {
              threshold = 1.5 + (3.0 - 1.5) * (gpsyInfo->sampleRate - 8000.0) / (44100.0 - 8000.0);
              if (threshold < 1.5) threshold = 1.5;
          }

          if ((volchg / toteng * quality) > threshold)
          {
              psyInfo->block_type = ONLY_SHORT_WINDOW;
              break;
          }
      }
      lasteng = eng;
  }

  psyInfo->pe = 0;
  {
      for (sfb = 0; sfb < lastband; sfb++)
      {
          faac_real enrg = 0;
          for (win = 0; win < 8; win++)
              enrg += psydata->eng[win][sfb];
          if (enrg > 0)
              psyInfo->pe += FAAC_LOG10(enrg);
      }
  }

#if PRINTSTAT
  frames.tot++;
  if (psyInfo->block_type == ONLY_SHORT_WINDOW)
      frames.s++;
#endif
}

static void PsyInit(GlobalPsyInfo * gpsyInfo, PsyInfo * psyInfo, unsigned int numChannels,
		    unsigned int sampleRate, int *cb_width_long, int num_cb_long,
		    int *cb_width_short, int num_cb_short)
{
  unsigned int channel;
  int i, j, size;
  Psy2Global *global = AllocMemory(sizeof(Psy2Global));

  gpsyInfo->hannWindow =
    (faac_real *) AllocMemory(2 * BLOCK_LEN_LONG * sizeof(faac_real));
  gpsyInfo->hannWindowS =
    (faac_real *) AllocMemory(2 * BLOCK_LEN_SHORT * sizeof(faac_real));

  for (i = 0; i < BLOCK_LEN_LONG * 2; i++)
    gpsyInfo->hannWindow[i] = 0.5 * (1 - FAAC_COS(2.0 * M_PI * (i + 0.5) /
					     (BLOCK_LEN_LONG * 2)));
  for (i = 0; i < BLOCK_LEN_SHORT * 2; i++)
    gpsyInfo->hannWindowS[i] = 0.5 * (1 - FAAC_COS(2.0 * M_PI * (i + 0.5) /
					      (BLOCK_LEN_SHORT * 2)));
  gpsyInfo->sampleRate = (faac_real) sampleRate;

  for (i = 0; i < BLOCK_LEN_LONG; i++)
  {
      faac_real freq = (faac_real)i * 18000.0 / BLOCK_LEN_LONG; // approx center freq
      faac_real fkHz = freq / 1000.0;
      faac_real ath;
      if (fkHz < 0.1) fkHz = 0.1;
      ath = 3.64 * FAAC_POW(fkHz, -0.8)
            - 6.5 * FAAC_EXP(-0.6 * FAAC_POW(fkHz - 3.3, 2.0))
            + 0.001 * FAAC_POW(fkHz, 4.0);

      global->ath_table[i] = FAAC_POW(10.0, (ath - 20.0) / 10.0);
  }
  gpsyInfo->data = global;

  for (channel = 0; channel < numChannels; channel++)
  {
    psydata_t *psydata = AllocMemory(sizeof(psydata_t));
    psyInfo[channel].data = psydata;
  }

  size = BLOCK_LEN_LONG;
  for (channel = 0; channel < numChannels; channel++)
  {
    psyInfo[channel].size = size;

    psyInfo[channel].prevSamples =
      (faac_real *) AllocMemory(size * sizeof(faac_real));
    memset(psyInfo[channel].prevSamples, 0, size * sizeof(faac_real));
    memset(psyInfo[channel].pns_state, 0, sizeof(psyInfo[channel].pns_state));
    psyInfo[channel].prev_block_type = -1;
  }

  size = BLOCK_LEN_SHORT;
  for (channel = 0; channel < numChannels; channel++)
  {
    psydata_t *psydata = psyInfo[channel].data;

    psyInfo[channel].sizeS = size;

    for (j = 0; j < 8; j++)
    {
      psydata->engPrev[j] =
            (psyfloat *) AllocMemory(NSFB_SHORT * sizeof(psyfloat));
      memset(psydata->engPrev[j], 0, NSFB_SHORT * sizeof(psyfloat));
      psydata->eng[j] =
          (psyfloat *) AllocMemory(NSFB_SHORT * sizeof(psyfloat));
      memset(psydata->eng[j], 0, NSFB_SHORT * sizeof(psyfloat));
      psydata->engNext[j] =
          (psyfloat *) AllocMemory(NSFB_SHORT * sizeof(psyfloat));
      memset(psydata->engNext[j], 0, NSFB_SHORT * sizeof(psyfloat));
      psydata->engNext2[j] =
          (psyfloat *) AllocMemory(NSFB_SHORT * sizeof(psyfloat));
      memset(psydata->engNext2[j], 0, NSFB_SHORT * sizeof(psyfloat));
    }
  }
}

static void PsyEnd(GlobalPsyInfo * gpsyInfo, PsyInfo * psyInfo, unsigned int numChannels)
{
  unsigned int channel;
  int j;

  if (gpsyInfo->data)
      FreeMemory(gpsyInfo->data);

  if (gpsyInfo->hannWindow)
    FreeMemory(gpsyInfo->hannWindow);
  if (gpsyInfo->hannWindowS)
    FreeMemory(gpsyInfo->hannWindowS);

  for (channel = 0; channel < numChannels; channel++)
  {
    if (psyInfo[channel].prevSamples)
      FreeMemory(psyInfo[channel].prevSamples);
  }

  for (channel = 0; channel < numChannels; channel++)
  {
    psydata_t *psydata = psyInfo[channel].data;

    for (j = 0; j < 8; j++)
    {
        if (psydata->engPrev[j])
            FreeMemory(psydata->engPrev[j]);
        if (psydata->eng[j])
            FreeMemory(psydata->eng[j]);
        if (psydata->engNext[j])
            FreeMemory(psydata->engNext[j]);
        if (psydata->engNext2[j])
            FreeMemory(psydata->engNext2[j]);
    }
  }

  for (channel = 0; channel < numChannels; channel++)
  {
    if (psyInfo[channel].data)
      FreeMemory(psyInfo[channel].data);
  }

#if PRINTSTAT
  printf("short frames: %d/%d (%.2f %%)\n", frames.s, frames.tot, 100.0*frames.s/frames.tot);
#endif
}

/* Do psychoacoustical analysis */
static void PsyCalculate(ChannelInfo * channelInfo, GlobalPsyInfo * gpsyInfo,
			 PsyInfo * psyInfo, int *cb_width_long, int
			 num_cb_long, int *cb_width_short,
			 int num_cb_short, unsigned int numChannels,
			 faac_real quality
			)
{
  unsigned int channel;

  // limit switching threshold
  if (quality < 0.4)
      quality = 0.4;

  for (channel = 0; channel < numChannels; channel++)
  {
    if (channelInfo[channel].present)
    {

      if (channelInfo[channel].cpe &&
	  channelInfo[channel].ch_is_left)
      {				/* CPE */

	int leftChan = channel;
	int rightChan = channelInfo[channel].paired_ch;

	PsyCheckShort(gpsyInfo, &psyInfo[leftChan], quality);
	PsyCheckShort(gpsyInfo, &psyInfo[rightChan], quality);
      }
      else if (!channelInfo[channel].cpe &&
	       channelInfo[channel].lfe)
      {				/* LFE */
        // Only set block type and it should be OK
	psyInfo[channel].block_type = ONLY_LONG_WINDOW;
      }
      else if (!channelInfo[channel].cpe)
      {				/* SCE */
	PsyCheckShort(gpsyInfo, &psyInfo[channel], quality);
      }
    }
  }
}

static void PsyBufferUpdate( FFT_Tables *fft_tables, GlobalPsyInfo * gpsyInfo, PsyInfo * psyInfo,
			    faac_real *newSamples, unsigned int bandwidth,
			    int *cb_width_short, int num_cb_short)
{
  int win;
  faac_real transBuff[2 * BLOCK_LEN_LONG];
  faac_real transBuffS[2 * BLOCK_LEN_SHORT];
  psydata_t *psydata = psyInfo->data;
  psyfloat *tmp;
  int sfb;

  psydata->bandS = psyInfo->sizeS * bandwidth * 2 / gpsyInfo->sampleRate;

  memcpy(transBuff, psyInfo->prevSamples, psyInfo->size * sizeof(faac_real));
  memcpy(transBuff + psyInfo->size, newSamples, psyInfo->size * sizeof(faac_real));

  for (win = 0; win < 8; win++)
  {
    int first = 0;
    int last = 0;

    memcpy(transBuffS, transBuff + (win * BLOCK_LEN_SHORT) + (BLOCK_LEN_LONG - BLOCK_LEN_SHORT) / 2,
	   2 * psyInfo->sizeS * sizeof(faac_real));

    Hann(gpsyInfo, transBuffS, 2 * psyInfo->sizeS);
    MDCT( fft_tables, transBuffS, 2 * psyInfo->sizeS);

    // shift bufs
    tmp = psydata->engPrev[win];
    psydata->engPrev[win] = psydata->eng[win];
    psydata->eng[win] = psydata->engNext[win];
    psydata->engNext[win] = psydata->engNext2[win];
    psydata->engNext2[win] = tmp;

    for (sfb = 0; sfb < num_cb_short; sfb++)
    {
      faac_real e;
      int l;

      first = last;
      last = first + cb_width_short[sfb];

      if (first < 1)
          first = 1;

      if (first >= psydata->bandS) // band out of range
          break;

      e = 0.0;
      for (l = first; l < last; l++)
          e += transBuffS[l] * transBuffS[l];

      psydata->engNext2[win][sfb] = e;
    }
    psydata->lastband = sfb;
    for (; sfb < num_cb_short; sfb++)
    {
        psydata->engNext2[win][sfb] = 0;
    }
  }

  memcpy(psyInfo->prevSamples, newSamples, psyInfo->size * sizeof(faac_real));
}

static void BlockSwitch(CoderInfo * coderInfo, PsyInfo * psyInfo, unsigned int numChannels)
{
  unsigned int channel;
  int desire = ONLY_LONG_WINDOW;

  /* Use the same block type for all channels
     If there is 1 channel that wants a short block,
     use a short block on all channels.
   */
  for (channel = 0; channel < numChannels; channel++)
  {
    if (psyInfo[channel].block_type == ONLY_SHORT_WINDOW)
      desire = ONLY_SHORT_WINDOW;
  }

  for (channel = 0; channel < numChannels; channel++)
  {
    int lasttype = coderInfo[channel].block_type;

    if (desire == ONLY_SHORT_WINDOW
	|| coderInfo[channel].desired_block_type == ONLY_SHORT_WINDOW)
    {
      if (lasttype == ONLY_LONG_WINDOW || lasttype == SHORT_LONG_WINDOW)
	coderInfo[channel].block_type = LONG_SHORT_WINDOW;
      else
	coderInfo[channel].block_type = ONLY_SHORT_WINDOW;
    }
    else
    {
      if (lasttype == ONLY_SHORT_WINDOW || lasttype == LONG_SHORT_WINDOW)
	coderInfo[channel].block_type = SHORT_LONG_WINDOW;
      else
	coderInfo[channel].block_type = ONLY_LONG_WINDOW;
    }
    coderInfo[channel].desired_block_type = desire;
  }
}

#define NOISEFLOOR 0.4

static void PsyMask(GlobalPsyInfo * gpsyInfo, PsyInfo * psyInfo, CoderInfo * __restrict coderInfo, faac_real * __restrict xr0, faac_real * __restrict bandlvl,
                  faac_real * __restrict bandenrg, int gnum, faac_real quality, int spreading, int athLevel,
                  faac_real * __restrict tonality, faac_real * __restrict bandlvl_stable)
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
  Psy2Global *global = (Psy2Global *)gpsyInfo->data;

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

    /* Optimized tonality: simplified peak-to-average ratio */
    if (avge > 0.0001)
        tonality[sfb] = maxe * (end - start) / avge;
    else
        tonality[sfb] = 0;

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
    if (bandlvl_stable)
        bandlvl_stable[sfb] = target;
  }

  /* Standard-aligned frequency spreading (energy domain) */
  if (spreading > 0)
  {
      faac_real spread[MAX_SCFAC_BANDS];
      /* Approximate spreading slopes: masking-down (lower freq) ~30dB/bark,
         masking-up (higher freq) ~15dB/bark.
         Scaled by level (5 = default). */
      for (sfb = 0; sfb < coderInfo->sfbn; sfb++)
      {
          faac_real s = bandenrg[sfb];
          if (sfb > 0) s = max(s, bandenrg[sfb - 1] * 0.02 * spreading); // ~17dB masking-up
          if (sfb < coderInfo->sfbn - 1) s = max(s, bandenrg[sfb + 1] * 0.01 * spreading); // ~20dB masking-down
          spread[sfb] = s;
      }
      for (sfb = 0; sfb < coderInfo->sfbn; sfb++)
          bandenrg[sfb] = spread[sfb];
  }

  if (athLevel > 0)
  {
      /* High-precision ATH suppression using precomputed table */
      faac_real sensitivity = athLevel * 0.1;
      for (sfb = 0; sfb < coderInfo->sfbn; sfb++)
      {
          int bcenter = (coderInfo->sfb_offset[sfb] + coderInfo->sfb_offset[sfb + 1]) >> 1;
          if (bcenter >= BLOCK_LEN_LONG) bcenter = BLOCK_LEN_LONG - 1;

          if (bandenrg[sfb] < global->ath_table[bcenter] * sensitivity)
              bandlvl[sfb] = 0.0;
      }
  }
}

psymodel_t psymodel2 =
{
  PsyInit,
  PsyEnd,
  PsyCalculate,
  PsyBufferUpdate,
  BlockSwitch,
  PsyMask
};
