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

  /* transient detector state */
  faac_real prev_energy;
  faac_real prev_sub_energies[32];
  int decision_delay[4];
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

  /* decision_delay[0] corresponds to the frame that is 'current' now
     relative to the lookahead analysis.
     Lookahead: next3SampleBuff (future +2)
     delay[2]: decision for future +2
     delay[1]: decision for future +1
     delay[0]: decision for current frame
  */

  if (psydata->decision_delay[0])
      psyInfo->block_type = ONLY_SHORT_WINDOW;
  else
      psyInfo->block_type = ONLY_LONG_WINDOW;

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
  psydata_t *psydata = (psydata_t *)psyInfo->data;
  int i;
  faac_real total_energy = 0;
  int is_transient = 0;

  /* Lookahead: newSamples (1024) is the future frame */
  for (i = 0; i < BLOCK_LEN_LONG; i++)
      total_energy += newSamples[i] * newSamples[i];

  /* Stage 1: Energy Pre-gate */
  if (total_energy > 2.5 * psydata->prev_energy && total_energy > 1e-3)
  {
      /* Stage 2: Spectral Analysis */
      faac_real fft_buf[BLOCK_LEN_LONG];
      faac_real flux = 0;
      faac_real max_sub_ratio = 0;
      int sub_energies_idx = 0;
      int start_bin = (int)(5000.0 * BLOCK_LEN_LONG / gpsyInfo->sampleRate);
      int end_bin = (int)(16000.0 * BLOCK_LEN_LONG / gpsyInfo->sampleRate);

      if (end_bin > BLOCK_LEN_LONG / 2) end_bin = BLOCK_LEN_LONG / 2;

      memcpy(fft_buf, newSamples, BLOCK_LEN_LONG * sizeof(faac_real));
      Hann(gpsyInfo, fft_buf, BLOCK_LEN_LONG);
      rfft(fft_tables, fft_buf, 10);

      /* Sub-band Energy Flux (5-16kHz) */
      /* Note: libfaac's rfft output is in natural frequency order because it
         internaly bit-reverses the input for the DIT FFT.
      */
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

      /* Tonal Veto: Variance-based approximation (Sum-of-Squares vs Square-of-Sums)
         Highly tonal = energy concentrated in few bins = high variance/arithmetic mean ratio.
      */
      faac_real sum_sq = 0;
      faac_real sq_sum = 0;
      int n_bins = BLOCK_LEN_LONG / 2 - 1;
      for (i = 1; i < BLOCK_LEN_LONG / 2; i++)
      {
          faac_real mag_sq = fft_buf[i] * fft_buf[i] + fft_buf[512 + i] * fft_buf[512 + i];
          sum_sq += mag_sq * mag_sq;
          sq_sum += mag_sq;
      }

      /* Normalized variance: E[X^2] / (E[X])^2. */
      faac_real norm_var = (sum_sq * n_bins) / (sq_sum * sq_sum + 1e-15);

      /* Trigger conditions: high flux OR massive single-band jump, AND not too tonal
         norm_var < 10.0 is a safe threshold to allow some tonality but block pure tones.
      */
      if (norm_var < 10.0 && (flux > 0.5 || max_sub_ratio > 1.25))
      {
          is_transient = 1;
      }
  }
  else
  {
      /* Pre-gate not tripped: update sub-energy baseline by scaling with total energy ratio
         to keep it reasonably fresh without an expensive FFT.
      */
      faac_real scale = total_energy / (psydata->prev_energy + 1e-15);
      int idx;
      for (idx = 0; idx < 32; idx++)
          psydata->prev_sub_energies[idx] *= scale;
  }

  psydata->prev_energy = total_energy;

  /* Shift decision delay line (3 stages) */
  psydata->decision_delay[0] = psydata->decision_delay[1];
  psydata->decision_delay[1] = psydata->decision_delay[2];
  psydata->decision_delay[2] = is_transient;

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
