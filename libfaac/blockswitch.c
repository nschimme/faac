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
#include <string.h>

#include "blockswitch.h"
#include "coder.h"
#include "util.h"
#include <faac.h>

typedef float psyfloat;

typedef struct
{
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

/* The high-pass first difference (d[n]=x[n]-x[n-1]) de-weights bass, whose
 * broadband energy would otherwise mask HF attacks and false-trigger short
 * blocks on stationary music; what's left tracks the band where pre-echo is
 * audible. A relative energy jump between sub-blocks past this threshold is a
 * transient. */
#define PSY_TD_THRESH ((faac_real)0.5)

static void PsyCheckShort(PsyInfo * psyInfo)
{
  enum {PREVS = 2, NEXTS = 2};
  psydata_t *psydata = psyInfo->data;
  int lastband = psydata->lastband;  /* time-domain energy lives at band 0 only */
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

          for (sfb = 0; sfb < lastband; sfb++)
          {
              toteng += (eng[sfb] < lasteng[sfb]) ? eng[sfb] : lasteng[sfb];
              volchg += FAAC_FABS(eng[sfb] - lasteng[sfb]);
          }

          if (volchg / toteng > PSY_TD_THRESH)
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

static void PsyInit(GlobalPsyInfo * gpsyInfo, PsyInfo * psyInfo, unsigned int numChannels,
		    unsigned int sampleRate, int *cb_width_long, int num_cb_long,
		    int *cb_width_short, int num_cb_short)
{
  unsigned int channel;
  int j, size;

  gpsyInfo->sampleRate = (faac_real) sampleRate;

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

  (void)gpsyInfo;

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

  (void)quality;  /* the detector's threshold is fixed; kept only for the interface */

  for (channel = 0; channel < numChannels; channel++)
  {
    if (channelInfo[channel].present)
    {

      if (channelInfo[channel].type == ELEMENT_CPE &&
	  channelInfo[channel].ch_is_left)
      {				/* CPE */

	int leftChan = channel;
	int rightChan = channelInfo[channel].paired_ch;

	PsyCheckShort(&psyInfo[leftChan]);
	PsyCheckShort(&psyInfo[rightChan]);
      }
      else if (channelInfo[channel].type == ELEMENT_LFE)
      {				/* LFE */
        // Only set block type and it should be OK
	psyInfo[channel].block_type = ONLY_LONG_WINDOW;
      }
      else if (channelInfo[channel].type == ELEMENT_SCE)
      {				/* SCE */
	PsyCheckShort(&psyInfo[channel]);
      }
    }
  }
}

static void PsyBufferUpdate(GlobalPsyInfo * gpsyInfo, PsyInfo * psyInfo,
			    faac_real *newSamples)
{
  int win;
  faac_real *transBuff = gpsyInfo->sharedWorkBuffLong;
  psydata_t *psydata = psyInfo->data;
  psyfloat *tmp;

  memcpy(transBuff, psyInfo->prevSamples, psyInfo->size * sizeof(faac_real));
  memcpy(transBuff + psyInfo->size, newSamples, psyInfo->size * sizeof(faac_real));

  for (win = 0; win < 8; win++)
  {
    /* seg[-1] is in bounds (seg starts >= 448 samples in), so the first
     * difference carries across the sub-block boundary instead of resetting. */
    faac_real *seg = transBuff + (win * BLOCK_LEN_SHORT) + (BLOCK_LEN_LONG - BLOCK_LEN_SHORT) / 2;
    faac_real e = 0.0;
    int l, n = 2 * psyInfo->sizeS;

    // shift bufs
    tmp = psydata->engPrev[win];
    psydata->engPrev[win] = psydata->eng[win];
    psydata->eng[win] = psydata->engNext[win];
    psydata->engNext[win] = psydata->engNext2[win];
    psydata->engNext2[win] = tmp;

    for (l = 0; l < n; l++)
    {
      faac_real d = seg[l] - seg[l - 1];
      e += d * d;
    }
    psydata->engNext2[win][0] = e;
    psydata->lastband = 1;
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
