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
#include <string.h>

#include "blockswitch.h"
#include "coder.h"
#include "fft.h"
#include "filtbank.h"
#include "util.h"
#include "faac_real.h"
#include <faac.h>

typedef float psyfloat;

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



#define PRINTSTAT 0
#if PRINTSTAT
static struct {
    int tot;
    int s;
} frames;
#endif

static void PsyCheckShort(PsyInfo * psyInfo, faac_real quality)
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

          if (toteng > 0.0 && (volchg / toteng * quality) > 3.0)
          {
              psyInfo->block_type = ONLY_SHORT_WINDOW;
              break;
          }
      }
      lasteng = eng;
  }

#if PRINTSTAT
  frames.tot++;
  if (psyInfo->block_type == ONLY_SHORT_WINDOW)
      frames.s++;
#endif
}

static faac_real get_ath(faac_real f) {
  f /= 1000.0; // kHz
  if (f < 0.1) f = 0.1;
  // Standard Terhardt formula (1979) for Absolute Threshold of Hearing
  return 3.64 * FAAC_POW(f, -0.8) - 6.5 * FAAC_EXP(-0.6 * (f - 3.3) * (f - 3.3)) + 0.001 * FAAC_POW(f, 4);
}

static void PsyInit(GlobalPsyInfo * gpsyInfo, PsyInfo * psyInfo, unsigned int numChannels,
		    unsigned int sampleRate, int *cb_width_long, int num_cb_long,
		    int *cb_width_short, int num_cb_short)
{
  unsigned int channel;
  int i, j, size;
  int start, end;

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

  /* Precompute ATH for long blocks */
  start = 0;
  for (i = 0; i < num_cb_long; i++) {
    end = start + cb_width_long[i];
    faac_real center_f = (faac_real)(start + end) / 2.0 * gpsyInfo->sampleRate / (2.0 * BLOCK_LEN_LONG);
    faac_real ath_val = get_ath(center_f);
    /* Scale ATH to heuristic energy domain matching bmask expectations */
    gpsyInfo->ath_long[i] = FAAC_POW(10.0, (ath_val - 25.0) / 10.0);
    start = end;
  }

  /* Precompute ATH for short blocks */
  start = 0;
  for (i = 0; i < num_cb_short; i++) {
    end = start + cb_width_short[i];
    faac_real center_f = (faac_real)(start + end) / 2.0 * gpsyInfo->sampleRate / (2.0 * BLOCK_LEN_SHORT);
    faac_real ath_val = get_ath(center_f);
    /* Scale ATH to heuristic energy domain matching bmask expectations */
    gpsyInfo->ath_short[i] = FAAC_POW(10.0, (ath_val - 25.0) / 10.0);
    gpsyInfo->inv_width_short[i] = 1.0 / (faac_real)(end - start);
    start = end;
  }

  /* Precompute long block inverse widths */
  start = 0;
  for (i = 0; i < num_cb_long; i++) {
    end = start + cb_width_long[i];
    gpsyInfo->inv_width_long[i] = 1.0 / (faac_real)(end - start);
    start = end;
  }

  /* Precompute sfacfix LUT */
  {
#if !defined(__clang__) && defined(__GNUC__) && (GCC_VERSION >= 40600)
    static const faac_real sfstep = 1.0 / FAAC_LOG10(FAAC_SQRT(FAAC_SQRT(2.0)));
#else
    static const faac_real sfstep = 20.0 / 1.50515;
#endif
    for (i = -100; i <= 100; i++) {
        if ((SF_OFFSET - i) < 10)
            gpsyInfo->sfacfix_lut[i + 100] = 0.0;
        else
            gpsyInfo->sfacfix_lut[i + 100] = FAAC_POW(10.0, (faac_real)i / sfstep);
    }
  }

  for (channel = 0; channel < numChannels; channel++)
  {
    psydata_t *psydata = AllocMemory(sizeof(psydata_t));
    memset(psydata, 0, sizeof(psydata_t));
    psyInfo[channel].data = psydata;
  }

  size = BLOCK_LEN_LONG;
  for (channel = 0; channel < numChannels; channel++)
  {
    psyInfo[channel].size = size;

    psyInfo[channel].prevSamples =
      (faac_real *) AllocMemory(size * sizeof(faac_real));
    memset(psyInfo[channel].prevSamples, 0, size * sizeof(faac_real));
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

	PsyCheckShort(&psyInfo[leftChan], quality);
	PsyCheckShort(&psyInfo[rightChan], quality);
      }
      else if (!channelInfo[channel].cpe &&
	       channelInfo[channel].lfe)
      {				/* LFE */
        // Only set block type and it should be OK
	psyInfo[channel].block_type = ONLY_LONG_WINDOW;
      }
      else if (!channelInfo[channel].cpe)
      {				/* SCE */
	PsyCheckShort(&psyInfo[channel], quality);
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
  psydata_t *psydata = psyInfo->data;
  psyfloat *tmp;
  int sfb;

  psydata->bandS = psyInfo->sizeS * bandwidth * 2 / gpsyInfo->sampleRate;

  memcpy(transBuff, psyInfo->prevSamples, psyInfo->size * sizeof(faac_real));
  memcpy(transBuff + psyInfo->size, newSamples, psyInfo->size * sizeof(faac_real));

  /* Transient detection in time domain (Portable performance improvement)
     Standard approach: look for sudden energy increase in high-pass filtered signal.
     This eliminates 8 MDCTs per channel per frame.
   */
  for (win = 0; win < 8; win++)
  {
    faac_real *winPtr = transBuff + (win * BLOCK_LEN_SHORT) + (BLOCK_LEN_LONG - BLOCK_LEN_SHORT) / 2;
    faac_real energy = 0.0;
    int i;

    /* Simple high-pass filter: x[n] - x[n-1] */
    for (i = 1; i < 2 * BLOCK_LEN_SHORT; i++) {
        faac_real diff = winPtr[i] - winPtr[i-1];
        energy += diff * diff;
    }

    // shift bufs
    tmp = psydata->engPrev[win];
    psydata->engPrev[win] = psydata->eng[win];
    psydata->eng[win] = psydata->engNext[win];
    psydata->engNext[win] = psydata->engNext2[win];
    psydata->engNext2[win] = tmp;

    /* We fake the subband energy by distributing the high-pass energy across bands.
       This is sufficient for the volume-change heuristic in PsyCheckShort.
     */
    faac_real avg_e = energy / (faac_real)num_cb_short;
    for (sfb = 0; sfb < num_cb_short; sfb++)
    {
      psydata->engNext2[win][sfb] = avg_e;
    }
    psydata->lastband = num_cb_short;
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

psymodel_t psymodel2 =
{
  PsyInit,
  PsyEnd,
  PsyCalculate,
  PsyBufferUpdate,
  BlockSwitch
};
