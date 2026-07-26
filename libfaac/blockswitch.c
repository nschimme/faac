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
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "blockswitch.h"
#include "coder.h"
#include "util.h"
#include "faac_internal.h"
#include "frame.h"

typedef float psyfloat;

/* The high-pass energy timeline is held as one contiguous array of per-sub-block
   energies rather than separate prev/curr/next arrays, so PsyCheckShort's +-2
   sub-block lookahead is a single sliding index instead of three-way stitching.
   It holds three 2-frame energy windows back to back: PREV, CUR and the one
   lookahead window NEXT. (Energy windows are 2 frames wide, which is why a single
   "next" window consumes the two-frames-ahead sample slot in the input FIFO.) */

/* The high-pass first difference (d[n]=x[n]-x[n-1]) de-weights bass, whose
 * broadband energy would otherwise mask HF attacks and false-trigger short
 * blocks on stationary music; what's left tracks the band where pre-echo is
 * audible. A relative energy jump between sub-blocks past this threshold is a
 * transient. */

#define PSY_TD_HARD (2.0f)
#define PSY_TD_HARD_MIN_SR 32000

void PsySetTdHard(PsyInfo *psyInfo, unsigned int numChannels, int tnsActive, unsigned int sampleRate)
{
  float hard = PSY_TD_THRESH;
  unsigned int ch;

  if (tnsActive && sampleRate >= (unsigned int)PSY_TD_HARD_MIN_SR)
    hard = PSY_TD_HARD;
  for (ch = 0; ch < numChannels; ch++) {
    psyInfo[ch].td_hard = hard;
    psyInfo[ch].tns_active = tnsActive;
  }
}

static void PsyCheckShort(PsyInfo * psyInfo)
{
  enum {PREVS = 2, NEXTS = 2};
  psydata_t *psydata = (psydata_t *)psyInfo->data;
  int win;
  float lasteng = (float)psydata->eng[ENG_WIN_CUR - PREVS]; /* start at PREVS before current */

  psyInfo->block_type = ONLY_LONG_WINDOW;
  psyInfo->tns_alert = 0;

  /* Search for transients across the current frame and its immediate temporal context.
     The search range is [curr-2, curr+9]. */
  for (win = 1; win < PREVS + SUBBLOCKS_PER_FRAME + NEXTS; win++)
  {
      float eng = (float)psydata->eng[ENG_WIN_CUR - PREVS + win];

      float toteng = (eng < lasteng) ? eng : lasteng;
      float volchg = fabsf(eng - lasteng);

      /* Relative energy jump indicates a transient. IEEE divide handles silence cases. */
      float strength = volchg / (toteng + 1e-9f);
      if (strength > PSY_TD_THRESH)
      {
          psyInfo->tns_alert = 1; /* Serve as TNS alert */

          if (psyInfo->tns_active) {
              /* Decompose transient strength into low and high bands to evaluate frequency spread */
              float eng_L = (float)psydata->eng_low[ENG_WIN_CUR - PREVS + win];
              float lasteng_L = (float)psydata->eng_low[ENG_WIN_CUR - PREVS + win - 1];
              float toteng_L = (eng_L < lasteng_L) ? eng_L : lasteng_L;
              float volchg_L = fabsf(eng_L - lasteng_L);
              float strength_L = volchg_L / (toteng_L + 1e-9f);

              float eng_H = (float)psydata->eng_high[ENG_WIN_CUR - PREVS + win];
              float lasteng_H = (float)psydata->eng_high[ENG_WIN_CUR - PREVS + win - 1];
              float toteng_H = (eng_H < lasteng_H) ? eng_H : lasteng_H;
              float volchg_H = fabsf(eng_H - lasteng_H);
              float strength_H = volchg_H / (toteng_H + 1e-9f);

              int is_broadband = (strength_L > PSY_TD_THRESH && strength_H > PSY_TD_THRESH);

              /* Switch to short window only if the transient is broadband AND exceeds td_hard */
              if (is_broadband && strength > psyInfo->td_hard) {
                  psyInfo->block_type = ONLY_SHORT_WINDOW;
                  break;
              }
          } else {
              /* Standard block switching: switch to short window immediately if above td_hard */
              if (strength > psyInfo->td_hard) {
                  psyInfo->block_type = ONLY_SHORT_WINDOW;
                  break;
              }
          }
      }
      lasteng = eng;
  }
}

void PsyInit(GlobalPsyInfo * gpsyInfo, PsyInfo * psyInfo, unsigned int numChannels,
		    unsigned int sampleRate)
{
  unsigned int channel;
  int size;

  gpsyInfo->sampleRate = (float) sampleRate;

  for (channel = 0; channel < numChannels; channel++)
  {
    psydata_t *psydata = (psydata_t *)AllocMemory(sizeof(psydata_t));
    if (!psydata) return;
    memset(psydata, 0, sizeof(psydata_t));
    psyInfo[channel].data = psydata;
    psyInfo[channel].td_hard = PSY_TD_THRESH;
  }

  size = BLOCK_LEN_LONG;
  for (channel = 0; channel < numChannels; channel++)
  {
    psyInfo[channel].size = size;
  }

  size = BLOCK_LEN_SHORT;
  for (channel = 0; channel < numChannels; channel++)
    psyInfo[channel].sizeS = size;
}

void PsyEnd(PsyInfo * psyInfo, unsigned int numChannels)
{
  unsigned int channel;

  for (channel = 0; channel < numChannels; channel++)
  {
    if (psyInfo[channel].data)
      FreeMemory(psyInfo[channel].data);
  }
}

/* Do psychoacoustical analysis */
void PsyCalculate(AACElement * elements, int numElements, PsyInfo * psyInfo,
			 unsigned int numChannels
			)
{
  if (elements == NULL) {
      for (unsigned int channel = 0; channel < numChannels; channel++)
          PsyCheckShort(&psyInfo[channel]);
      return;
  }

  for (int e = 0; e < numElements; e++)
  {
      AACElement *elem = &elements[e];
      switch (elem->type) {
          case ID_SCE:
              PsyCheckShort(&psyInfo[elem->channels[0]]);
              break;
          case ID_CPE:
              PsyCheckShort(&psyInfo[elem->channels[0]]);
              PsyCheckShort(&psyInfo[elem->channels[1]]);
              break;
          case ID_LFE:
              psyInfo[elem->channels[0]].block_type = ONLY_LONG_WINDOW;
              break;
          default:
              break;
      }
  }
}

void PsyBufferUpdate(GlobalPsyInfo * gpsyInfo, PsyInfo * psyInfo,
                            float * restrict p_lookahead1,
                            float * restrict p_lookahead2)
{
  int win;
  float * restrict transBuff = gpsyInfo->sharedWorkBuffLong;
  psydata_t *psydata = (psydata_t *)psyInfo->data;
  float alpha = 0.207f; /* default for 44.1k/48k */

  if (psyInfo->tns_active && gpsyInfo->sampleRate > 0.0f) {
      float wc = 2.0f * 3.14159265f * 2000.0f / gpsyInfo->sampleRate;
      alpha = wc / (1.0f + wc);
  }

  /* Shift the energy windows down by one frame: PREV<-CUR, CUR<-NEXT, freeing
     the NEXT region for the freshly-computed lookahead window below. */
  memmove(psydata->eng, psydata->eng + SUBBLOCKS_PER_FRAME,
          2 * SUBBLOCKS_PER_FRAME * sizeof(psyfloat));
  if (psyInfo->tns_active) {
    memmove(psydata->eng_low, psydata->eng_low + SUBBLOCKS_PER_FRAME,
            2 * SUBBLOCKS_PER_FRAME * sizeof(float));
    memmove(psydata->eng_high, psydata->eng_high + SUBBLOCKS_PER_FRAME,
            2 * SUBBLOCKS_PER_FRAME * sizeof(float));
  }

  /* Assembly of the newest 2048-sample window for energy analysis */
  memcpy(transBuff, p_lookahead1, BLOCK_LEN_LONG * sizeof(float));
  memcpy(transBuff + BLOCK_LEN_LONG, p_lookahead2, BLOCK_LEN_LONG * sizeof(float));

  for (win = 0; win < SUBBLOCKS_PER_FRAME; win++)
  {
    /* seg[-1] is in bounds (seg starts >= 448 samples in), so the first
     * difference carries across the sub-block boundary instead of resetting. */
    float *seg = transBuff + (win * BLOCK_LEN_SHORT) + (BLOCK_LEN_LONG - BLOCK_LEN_SHORT) / 2;
    float e = 0.0f;
    int l, n = 2 * psyInfo->sizeS;

    if (psyInfo->tns_active)
    {
      float e_low = 0.0f, e_high = 0.0f;
      float d_L = 0.0f;

      for (l = 0; l < n; l++)
      {
        float d = seg[l] - seg[l - 1];
        e += d * d;

        /* Crossover filter splitting */
        d_L = d_L + alpha * (d - d_L);
        float d_H = d - d_L;
        e_low += d_L * d_L;
        e_high += d_H * d_H;
      }
      psydata->eng_low[ENG_WIN_NEXT + win] = e_low;
      psydata->eng_high[ENG_WIN_NEXT + win] = e_high;
    }
    else
    {
      for (l = 0; l < n; l++)
      {
        float d = seg[l] - seg[l - 1];
        e += d * d;
      }
    }
    psydata->eng[ENG_WIN_NEXT + win] = (psyfloat)e;
  }
}

void BlockSwitch(struct faacEncStruct *hEncoder, CoderInfo * coderInfo, PsyInfo * psyInfo, unsigned int numChannels)
{
  unsigned int channel;
  int desire = ONLY_LONG_WINDOW;

  /* Shared transient override for HE-AAC path.
   * Core delay alignment: SbrAnalyze runs on frame N full-rate; core
   * block-switch for frame N audio is emitted at a delay. Alignment logic
   * uses the FIFO. */
  if (hEncoder->config.aacObjectType == HE_V1 && SbrContextIsAnalysisValid(hEncoder->sbrContext))
  {
      for (channel = 0; channel < numChannels; channel++)
      {
          /* Alignment: the core frame being coded now lags the freshest SBR
           * analysis by LOOKAHEAD_DEPTH frames; FIFO index 0 holds that frame's
           * decision (FIFO sized SBR_DETECT_FIFO so [0] is LOOKAHEAD_DEPTH back). */
          int wantShort = SbrContextGetWantShort(hEncoder->sbrContext, (int)channel, 0);

          if (wantShort)
              psyInfo[channel].block_type = ONLY_SHORT_WINDOW;
          else
              psyInfo[channel].block_type = ONLY_LONG_WINDOW;
      }
  }

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
