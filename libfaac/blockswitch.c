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
  int consecutive_short_counter;
  faac_real last_subwindow_energy;
  int is_first_frame;
  faac_real lookahead_score;
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
  psydata_t *psydata = (psydata_t *)psyInfo->data;
  faac_real score = psydata->lookahead_score;

  /* Step 5: Bitrate-aware Threshold Scaling */
  /* quality 1.0 roughly corresponds to 128 kbps aggregate (64kbps/ch)
     Scaling thresholds based on user recommendation:
     - < 96k: +0.05
     - < 64k: +0.10
     - < 48k: +0.15
   */
  faac_real threshold = 0.55;
  if (quality < 0.75) threshold += 0.05;
  if (quality < 0.50) threshold += 0.10;
  if (quality < 0.375) threshold += 0.15;

  int transient_detected = (score > threshold);

  /* Step 5, 6, 7: Block Switching Decision & Hysteresis */
  if (transient_detected)
  {
    psydata->short_block_counter = 3; /* Detection frame + 2 forced = 3 total */
    psydata->calm_frame_counter = 0;
  }
  else
  {
    psydata->calm_frame_counter += 1;
  }

  int use_short = 0;
  if (psydata->short_block_counter > 0)
  {
    use_short = 1;
    psydata->short_block_counter -= 1;
  }
  else if (psydata->calm_frame_counter >= 2)
  {
    use_short = 0;
  }
  else
  {
      /* Require two consecutive calm frames before switching back to long blocks */
      use_short = 1;
  }

  /* Safety Safeguard: Force return to long after 6 frames unless strong transient */
  if (use_short)
  {
      psydata->consecutive_short_counter++;
      if (psydata->consecutive_short_counter > 6 && score < (threshold + 0.2))
      {
          use_short = 0;
          psydata->consecutive_short_counter = 0;
      }
  }
  else
  {
      psydata->consecutive_short_counter = 0;
  }

  psyInfo->block_type = use_short ? ONLY_SHORT_WINDOW : ONLY_LONG_WINDOW;

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
			    faac_real * __restrict newSamples, unsigned int bandwidth,
			    int *cb_width_short, int num_cb_short)
{
  int win, i;
  psydata_t * __restrict psydata = (psydata_t *)psyInfo->data;
  const faac_real * __restrict win1024 = gpsyInfo->hannWindow1024;
  const faac_real eps = (faac_real)1e-9;
  const faac_real log_eps = (faac_real)1e-12;

  /* Step 1: Subwindow Energy Analysis */
  faac_real max_energy_ratio = 0.0;
  faac_real prev_e = psydata->last_subwindow_energy;
  const faac_real * __restrict s_ptr = newSamples;
  const faac_real energy_floor = (faac_real)1e-7;

  for (win = 0; win < 8; win++)
  {
    faac_real e = 0.0;
    for (i = 0; i < 128; i++)
    {
      faac_real s = s_ptr[i];
      e += s * s;
    }
    s_ptr += 128;
    if (win == 0 && psydata->is_first_frame) prev_e = e;

    if (e > energy_floor)
    {
      faac_real ratio = e / (prev_e + eps);
      if (ratio > max_energy_ratio) max_energy_ratio = ratio;
    }
    prev_e = e;
  }
  psydata->last_subwindow_energy = prev_e; /* Energy of last subwindow */

  const int is_first = psydata->is_first_frame;
  faac_real current_mag[512];

  faac_real energy_feature = max_energy_ratio * (1.0 / 3.0);
  if (energy_feature > 1.0) energy_feature = 1.0;

  /* Early exit for stationary frames to save power/CPU */
  if (max_energy_ratio < 1.1 && psydata->short_block_counter == 0 && !is_first)
  {
      psydata->lookahead_score = 0.0;
      /* Do NOT zero prev_mag here as it would cause a false transient in the next frame.
         The spectral flux calculation needs a valid baseline.
       */
      psydata->is_first_frame = 0;
      psydata->bandS = psyInfo->sizeS * bandwidth * 2 / gpsyInfo->sampleRate;
      return;
  }

  /* Step 2: Spectral Flux & Step 4: Tonality (Single spectral pass) */
  faac_real fft_buf[1024];
  for (i = 0; i < 1024; i++)
    fft_buf[i] = newSamples[i] * win1024[i];

  rfft(fft_tables, fft_buf, 10);

  faac_real sum_mag = 0.0;
  faac_real flux = 0.0;

  /* Invariant hoisted */
  const faac_real bin_width = gpsyInfo->sampleRate * (1.0 / 1024.0);
  int bin_limit = (int)FAAC_FLOOR(bandwidth / bin_width);
  if (bin_limit > 511) bin_limit = 511;

  const faac_real * __restrict prev_mag = psydata->prev_mag;

  /* Optimized spectral pass.
     rfft layout in libfaac/fft.c:
     xr contains the real parts [0..1023]
     rfft calls fft with zeroed xi, then:
     memcpy(xr + 512, xi, 512) copies the first 512 imaginary bins into the second half of xr.
     Bins 0 and 512 (RN/2) have no imaginary part for real FFT.
     Actually, index 512 in xr would be the imaginary part of bin 0 (which is 0).
     Let's use bin 0 to 511.
  */
  current_mag[0] = FAAC_FABS(fft_buf[0]);
  sum_mag = current_mag[0];
  if (!is_first) {
      faac_real diff = current_mag[0] - prev_mag[0];
      if (diff > 0) flux = diff;
  }

  for (i = 1; i < 512; i++)
  {
    faac_real re = fft_buf[i];
    faac_real im = fft_buf[i + 512];
    faac_real mag = FAAC_SQRT(re * re + im * im);
    current_mag[i] = mag;
    if (i <= bin_limit) {
      sum_mag += mag;
      if (!is_first)
      {
        faac_real diff = mag - prev_mag[i];
        if (diff > 0) flux += diff;
      }
    }
  }

  faac_real normalized_flux = flux / (sum_mag + eps);

  /* Step 3: Combine Detectors */
  faac_real score = 0.6 * energy_feature + 0.4 * normalized_flux;

  /* Step 4: Noise Suppression (using SFM) */
  if (sum_mag > eps)
  {
    faac_real sum_log_mag = 0.0;
    for (i = 0; i <= bin_limit; i++)
        sum_log_mag += FAAC_LOG(current_mag[i] + log_eps);

    faac_real inv_n = 1.0 / (faac_real)(bin_limit + 1);
    faac_real geom_mean = FAAC_EXP(sum_log_mag * inv_n);
    faac_real arith_mean = sum_mag * inv_n;
    /* SFM -> 0.0 for pure tones, 1.0 for noise */
    faac_real sfm = geom_mean / (arith_mean + log_eps);

    /* if SFM > 0.6 (noise), reduce transient score */
    if (sfm > 0.6)
        score *= 0.75;
  }

  if (is_first) score = 0.0;

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
