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
#include "filtbank.h"
#include <faac.h>

typedef float psyfloat;

typedef struct
{
  /* bandwidth */
  int bandS;

  /* New transient detector state */
  faac_real prev_mag[512];
  int short_block_counter;
  int calm_frame_counter;
  faac_real last_subwindow_energy;
  int is_first_frame;
  faac_real lookahead_score;
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

static void PsyCheckShort(PsyInfo * psyInfo, faac_real quality)
{
  psydata_t *psydata = (psydata_t *)psyInfo->data;
  faac_real score = psydata->lookahead_score;

  /* Step 5, 6, 7: Block Switching Decision & Hysteresis */
  int transient_detected = (score > 0.5);

  if (transient_detected)
  {
    psydata->short_block_counter = 2;
    psydata->calm_frame_counter = 0;
  }
  else
  {
    psydata->calm_frame_counter += 1;
  }

  if (psydata->short_block_counter > 0)
  {
    psyInfo->block_type = ONLY_SHORT_WINDOW;
    psydata->short_block_counter -= 1;
  }
  else if (psydata->calm_frame_counter >= 2)
  {
    psyInfo->block_type = ONLY_LONG_WINDOW;
  }
  else
  {
      /* Require two consecutive calm frames before switching back to long blocks */
      psyInfo->block_type = ONLY_SHORT_WINDOW;
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

  gpsyInfo->hannWindow =
    (faac_real *) AllocMemory(2 * BLOCK_LEN_LONG * sizeof(faac_real));
  gpsyInfo->hannWindowS =
    (faac_real *) AllocMemory(2 * BLOCK_LEN_SHORT * sizeof(faac_real));
  gpsyInfo->hannWindow1024 =
    (faac_real *) AllocMemory(1024 * sizeof(faac_real));

  for (i = 0; i < BLOCK_LEN_LONG * 2; i++)
    gpsyInfo->hannWindow[i] = 0.5 * (1 - FAAC_COS(2.0 * M_PI * (i + 0.5) /
					     (BLOCK_LEN_LONG * 2)));
  for (i = 0; i < BLOCK_LEN_SHORT * 2; i++)
    gpsyInfo->hannWindowS[i] = 0.5 * (1 - FAAC_COS(2.0 * M_PI * (i + 0.5) /
					      (BLOCK_LEN_SHORT * 2)));
  for (i = 0; i < 1024; i++)
    gpsyInfo->hannWindow1024[i] = 0.5 * (1 - FAAC_COS(2.0 * M_PI * (i + 0.5) /
					      1024));
  gpsyInfo->sampleRate = (faac_real) sampleRate;

  for (channel = 0; channel < numChannels; channel++)
  {
    psydata_t *psydata = (psydata_t *)AllocMemory(sizeof(psydata_t));
    memset(psydata, 0, sizeof(psydata_t));
    psydata->is_first_frame = 1;
    psydata->calm_frame_counter = 2; /* Start in calm state */
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
    psyInfo[channel].sizeS = size;
  }
}

static void PsyEnd(GlobalPsyInfo * gpsyInfo, PsyInfo * psyInfo, unsigned int numChannels)
{
  unsigned int channel;

  if (gpsyInfo->hannWindow)
    FreeMemory(gpsyInfo->hannWindow);
  if (gpsyInfo->hannWindowS)
    FreeMemory(gpsyInfo->hannWindowS);
  if (gpsyInfo->hannWindow1024)
    FreeMemory(gpsyInfo->hannWindow1024);

  for (channel = 0; channel < numChannels; channel++)
  {
    if (psyInfo[channel].prevSamples)
      FreeMemory(psyInfo[channel].prevSamples);
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
  int win, i;
  psydata_t *psydata = (psydata_t *)psyInfo->data;

  /* Step 1: Subwindow Energy Analysis */
  faac_real max_energy_ratio = 0.0;
  faac_real prev_e = psydata->last_subwindow_energy;

  for (win = 0; win < 8; win++)
  {
    faac_real e = 0.0;
    for (i = 0; i < 128; i++)
    {
      faac_real s = newSamples[win * 128 + i];
      e += s * s;
    }
    if (win == 0 && psydata->is_first_frame) prev_e = e;

    faac_real ratio = e / (prev_e + 1e-9);
    if (ratio > max_energy_ratio) max_energy_ratio = ratio;
    prev_e = e;
  }
  psydata->last_subwindow_energy = prev_e; /* Energy of win 7 */

  faac_real energy_feature = max_energy_ratio / 3.0;
  if (energy_feature > 1.0) energy_feature = 1.0;

  /* Step 2: Spectral Flux */
  faac_real fft_buf[1024];
  memcpy(fft_buf, newSamples, 1024 * sizeof(faac_real));
  for (i = 0; i < 1024; i++)
    fft_buf[i] *= gpsyInfo->hannWindow1024[i];

  rfft(fft_tables, fft_buf, 10);

  faac_real current_mag[512];
  faac_real sum_mag = 0.0;
  faac_real flux = 0.0;

  current_mag[0] = FAAC_FABS(fft_buf[0]);
  for (i = 1; i < 512; i++)
  {
    current_mag[i] = FAAC_SQRT(fft_buf[i] * fft_buf[i] + fft_buf[i + 512] * fft_buf[i + 512]);
  }

  int bin_limit = (int)FAAC_FLOOR(bandwidth / (gpsyInfo->sampleRate / 1024.0));
  if (bin_limit > 511) bin_limit = 511;

  for (i = 0; i <= bin_limit; i++)
  {
    sum_mag += current_mag[i];
    if (!psydata->is_first_frame)
    {
      faac_real diff = current_mag[i] - psydata->prev_mag[i];
      if (diff > 0) flux += diff;
    }
  }
  faac_real normalized_flux = flux / (sum_mag + 1e-9);

  /* Step 4: Tonality Protection */
  faac_real tonality = 1.0;
  if (sum_mag > 1e-9)
  {
    faac_real sum_log_mag = 0.0;
    for (i = 0; i <= bin_limit; i++)
    {
      sum_log_mag += FAAC_LOG(current_mag[i] + (faac_real)1e-12);
    }
    faac_real geom_mean = FAAC_EXP(sum_log_mag / (faac_real)(bin_limit + 1));
    faac_real arith_mean = sum_mag / (faac_real)(bin_limit + 1);
    tonality = geom_mean / (arith_mean + 1e-12);
  }

  /* Step 3: Combine Detectors */
  faac_real score = 0.6 * energy_feature + 0.4 * normalized_flux;
  if (tonality < 0.2) score *= 0.7;

  if (psydata->is_first_frame) score = 0.0;

  /* Store for PsyCheckShort (immediate lookahead) */
  psydata->lookahead_score = score;

  /* Update State */
  memcpy(psydata->prev_mag, current_mag, 512 * sizeof(faac_real));
  psydata->is_first_frame = 0;

  psydata->bandS = psyInfo->sizeS * bandwidth * 2 / gpsyInfo->sampleRate;
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
