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

typedef struct
{
  /* transient detector state */
  faac_real prev_energy;
  faac_real prev_sub_energies[32];
  faac_real prev_subwindow_energy;

  /* Two-stage transient status for lookahead alignment */
  int next_transient;
  int curr_transient;
  int short_todo;
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
  else if (size == BLOCK_LEN_LONG)
  {
    for (i = 0; i < size; i++)
      inSamples[i] *= gpsyInfo->hannWindow1024[i];
  }
  else
  {
    for (i = 0; i < size; i++)
      inSamples[i] *= gpsyInfo->hannWindowS[i];
  }
}

static void PsyCheckShort(PsyInfo * psyInfo, faac_real quality)
{
  psydata_t *psydata = (psydata_t *)psyInfo->data;

  if (psydata->short_todo > 0)
      psyInfo->block_type = ONLY_SHORT_WINDOW;
  else
      psyInfo->block_type = ONLY_LONG_WINDOW;
}

static void PsyInit(GlobalPsyInfo * gpsyInfo, PsyInfo * psyInfo, unsigned int numChannels,
		    unsigned int sampleRate, int *cb_width_long, int num_cb_long,
		    int *cb_width_short, int num_cb_short)
{
  unsigned int channel;
  int i, size;

  gpsyInfo->hannWindow =
    (faac_real *) AllocMemory(2 * BLOCK_LEN_LONG * sizeof(faac_real));
  gpsyInfo->hannWindow1024 =
    (faac_real *) AllocMemory(BLOCK_LEN_LONG * sizeof(faac_real));
  gpsyInfo->hannWindowS =
    (faac_real *) AllocMemory(2 * BLOCK_LEN_SHORT * sizeof(faac_real));

  for (i = 0; i < BLOCK_LEN_LONG * 2; i++)
    gpsyInfo->hannWindow[i] = 0.5 * (1 - FAAC_COS(2.0 * M_PI * (i + 0.5) /
					     (BLOCK_LEN_LONG * 2)));
  for (i = 0; i < BLOCK_LEN_LONG; i++)
    gpsyInfo->hannWindow1024[i] = 0.5 * (1 - FAAC_COS(2.0 * M_PI * (i + 0.5) /
					     (BLOCK_LEN_LONG)));
  for (i = 0; i < BLOCK_LEN_SHORT * 2; i++)
    gpsyInfo->hannWindowS[i] = 0.5 * (1 - FAAC_COS(2.0 * M_PI * (i + 0.5) /
					      (BLOCK_LEN_SHORT * 2)));
  gpsyInfo->sampleRate = (faac_real) sampleRate;

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
    psyInfo[channel].sizeS = size;
  }
}

static void PsyEnd(GlobalPsyInfo * gpsyInfo, PsyInfo * psyInfo, unsigned int numChannels)
{
  unsigned int channel;

  if (gpsyInfo->hannWindow)
    FreeMemory(gpsyInfo->hannWindow);
  if (gpsyInfo->hannWindow1024)
    FreeMemory(gpsyInfo->hannWindow1024);
  if (gpsyInfo->hannWindowS)
    FreeMemory(gpsyInfo->hannWindowS);

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
  psydata_t *psydata = (psydata_t *)psyInfo->data;
  int i, win;
  faac_real total_energy = 0;
  int is_transient = 0;
  faac_real flux = 0;
  faac_real max_sub_ratio = 0;
  faac_real norm_var = 100.0;

  faac_real last_sub_e = psydata->prev_subwindow_energy;
  int sub_win_triggered = 0;
  for (win = 0; win < 8; win++)
  {
      faac_real sub_e = 0;
      int j;
      for (j = 0; j < 128; j++)
      {
          faac_real s = newSamples[win * 128 + j];
          sub_e += s * s;
      }
      if (sub_e > 10.0 * last_sub_e && sub_e > 1000.0)
          sub_win_triggered = 1;
      last_sub_e = sub_e;
      total_energy += sub_e;
  }
  psydata->prev_subwindow_energy = last_sub_e;

  if (sub_win_triggered)
  {
      faac_real fft_buf[BLOCK_LEN_LONG];
      int sub_energies_idx = 0;
      int start_bin = (int)(5000.0 * BLOCK_LEN_LONG / gpsyInfo->sampleRate);
      int end_bin = (int)(16000.0 * BLOCK_LEN_LONG / gpsyInfo->sampleRate);
      if (end_bin > BLOCK_LEN_LONG / 2) end_bin = BLOCK_LEN_LONG / 2;
      memcpy(fft_buf, newSamples, BLOCK_LEN_LONG * sizeof(faac_real));
      Hann(gpsyInfo, fft_buf, BLOCK_LEN_LONG);
      rfft(fft_tables, fft_buf, 10);
      for (i = start_bin; i < end_bin; i += 16)
      {
          faac_real sub_energy = 0;
          faac_real ratio;
          int j;
          for (j = i; j < i + 16 && j < end_bin; j++)
          {
              faac_real mag_sq = fft_buf[j] * fft_buf[j] + fft_buf[512 + j] * fft_buf[512 + j];
              sub_energy += mag_sq;
          }
          ratio = sub_energy / (psydata->prev_sub_energies[sub_energies_idx] + 1e-9);
          if (ratio > 1.0)
              flux += (ratio - 1.0);
          if (ratio > max_sub_ratio)
              max_sub_ratio = ratio;
          psydata->prev_sub_energies[sub_energies_idx] = sub_energy;
          sub_energies_idx++;
      }
      faac_real sum_sq = 0;
      faac_real sq_sum = 0;
      int n_bins = BLOCK_LEN_LONG / 2 - 1;
      for (i = 1; i < BLOCK_LEN_LONG / 2; i++)
      {
          faac_real mag_sq = fft_buf[i] * fft_buf[i] + fft_buf[512 + i] * fft_buf[512 + i];
          sum_sq += mag_sq * mag_sq;
          sq_sum += mag_sq;
      }
      norm_var = (sum_sq * n_bins) / (sq_sum * sq_sum + 1e-15);
  }
  else
  {
      faac_real scale = total_energy / (psydata->prev_energy + 1e-15);
      if (scale > 1.0) scale = 1.0;
      int idx;
      for (idx = 0; idx < 32; idx++)
          psydata->prev_sub_energies[idx] *= scale;
  }
  psydata->prev_energy = total_energy;
  psydata->curr_transient = psydata->next_transient;
  if (sub_win_triggered && norm_var < 3.0 && (flux > 50.0 || max_sub_ratio > 40.0))
      is_transient = 1;
  psydata->next_transient = is_transient;
  if (psydata->curr_transient)
      psydata->short_todo = 2;
  else if (psydata->short_todo > 0)
      psydata->short_todo--;
  memcpy(psyInfo->prevSamples, newSamples, psyInfo->size * sizeof(faac_real));
}

static void BlockSwitch(CoderInfo * coderInfo, PsyInfo * psyInfo, unsigned int numChannels)
{
  unsigned int channel;
  int desire = ONLY_LONG_WINDOW;
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
