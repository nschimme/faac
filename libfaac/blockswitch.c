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
#include "util.h"
#include <faac.h>

typedef struct
{
  faac_real en_prev[8];
  faac_real en_curr[8];
  faac_real en_next[8];
  faac_real en_next2[8];
  faac_real hp_mem;
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
  psydata_t *psydata = psyInfo->data;
  int win;
  faac_real lasteng;

  psyInfo->block_type = ONLY_LONG_WINDOW;

  lasteng = psydata->en_prev[7];
  for (win = 0; win < 16; win++)
  {
      faac_real eng;

      if (win < 8)
          eng = psydata->en_curr[win];
      else
          eng = psydata->en_next[win - 8];

      if (eng > (lasteng * (1.0 + 3.0 / quality)) && eng > 500000.0)
      {
          psyInfo->block_type = ONLY_SHORT_WINDOW;
          break;
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

  gpsyInfo->sampleRate = (faac_real) sampleRate;

  for (channel = 0; channel < numChannels; channel++)
  {
    psydata_t *psydata = AllocMemory(sizeof(psydata_t));
    psyInfo[channel].data = psydata;
    memset(psydata, 0, sizeof(psydata_t));
  }
}

static void PsyEnd(GlobalPsyInfo * gpsyInfo, PsyInfo * psyInfo, unsigned int numChannels)
{
  unsigned int channel;

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
  int win, i;
  psydata_t *psydata = psyInfo->data;

  // Shift energy buffers
  for (win = 0; win < 8; win++)
  {
    psydata->en_prev[win] = psydata->en_curr[win];
    psydata->en_curr[win] = psydata->en_next[win];
    psydata->en_next[win] = psydata->en_next2[win];
  }

  // Calculate new energies using high-pass filter
  for (win = 0; win < 8; win++)
  {
    faac_real en = 0.0;
    faac_real *s = newSamples + win * BLOCK_LEN_SHORT;

    for (i = 0; i < BLOCK_LEN_SHORT; i++)
    {
        faac_real hp_sample = s[i] - psydata->hp_mem;
        psydata->hp_mem = s[i];
        en += hp_sample * hp_sample;
    }
    psydata->en_next2[win] = en;
  }
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
