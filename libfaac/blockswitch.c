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
#define SUBBLOCKS_PER_FRAME 8
#define ENG_WIN_PREV (0 * SUBBLOCKS_PER_FRAME)
#define ENG_WIN_CUR  (1 * SUBBLOCKS_PER_FRAME)
#define ENG_WIN_NEXT (2 * SUBBLOCKS_PER_FRAME)

typedef struct
{
  psyfloat eng[3 * SUBBLOCKS_PER_FRAME];
  psyfloat eng_low[3 * SUBBLOCKS_PER_FRAME];
  psyfloat eng_high[3 * SUBBLOCKS_PER_FRAME];
}
psydata_t;

/* The high-pass first difference (d[n]=x[n]-x[n-1]) de-weights bass, whose
 * broadband energy would otherwise mask HF attacks and false-trigger short
 * blocks on stationary music; what's left tracks the band where pre-echo is
 * audible. A relative energy jump between sub-blocks past this threshold is a
 * transient. */
#define PSY_TD_THRESH (0.5f)
#define PSY_TD_HARD   (2.0f)
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
  float lastlow = (float)psydata->eng_low[ENG_WIN_CUR - PREVS];
  float lasthigh = (float)psydata->eng_high[ENG_WIN_CUR - PREVS];
  float strength_total = 0.0f;
  float strength_low = 0.0f;
  float strength_high = 0.0f;

  psyInfo->pending.block_type = ONLY_LONG_WINDOW;
  psyInfo->pending.use_tns = 1;

  /* Search for transients across the current frame and its immediate temporal context.
     The search range is [curr-2, curr+9]. */
  for (win = 1; win < PREVS + SUBBLOCKS_PER_FRAME + NEXTS; win++)
  {
      float eng = (float)psydata->eng[ENG_WIN_CUR - PREVS + win];
      float low = (float)psydata->eng_low[ENG_WIN_CUR - PREVS + win];
      float high = (float)psydata->eng_high[ENG_WIN_CUR - PREVS + win];

      float toteng = (eng < lasteng) ? eng : lasteng;
      float volchg = fabsf(eng - lasteng);
      float s_total = volchg / (toteng + 1e-9f);

      float totlow = (low < lastlow) ? low : lastlow;
      float vollow = fabsf(low - lastlow);
      float s_low = vollow / (totlow + 1e-9f);

      float tothigh = (high < lasthigh) ? high : lasthigh;
      float volhigh = fabsf(high - lasthigh);
      float s_high = volhigh / (tothigh + 1e-9f);

      if (s_total > strength_total) strength_total = s_total;
      if (s_low > strength_low) strength_low = s_low;
      if (s_high > strength_high) strength_high = s_high;

      lasteng = eng;
      lastlow = low;
      lasthigh = high;
  }

  int transient_total = (strength_total > PSY_TD_THRESH);
  int transient_low = (strength_low > PSY_TD_THRESH);
  int transient_high = (strength_high > PSY_TD_THRESH);

  if (transient_total)
  {
      if (transient_low && transient_high)
      {
          // Broadband attack
          if (psyInfo->tns_active)
          {
              if (strength_total > psyInfo->td_hard)
              {
                  psyInfo->pending.block_type = ONLY_SHORT_WINDOW;
                  psyInfo->pending.use_tns = 0;
              }
              else
              {
                  psyInfo->pending.block_type = ONLY_LONG_WINDOW;
                  psyInfo->pending.use_tns = 1; // Aggressively evaluate TNS (TNS alert)
              }
          }
          else
          {
              psyInfo->pending.block_type = ONLY_SHORT_WINDOW;
              psyInfo->pending.use_tns = 0;
          }
      }
      else if (transient_high)
      {
          // High-Band-Only Spike.
          if (psyInfo->tns_active)
          {
              // Suppress short block, force LONG, and let TNS handle it.
              psyInfo->pending.block_type = ONLY_LONG_WINDOW;
              psyInfo->pending.use_tns = 1;
          }
          else
          {
              // TNS not active: cannot handle it, so we must allow short block.
              psyInfo->pending.block_type = ONLY_SHORT_WINDOW;
              psyInfo->pending.use_tns = 0;
          }
      }
      else
      {
          // Low-Band-Only Spike. Suppress short block, force LONG.
          psyInfo->pending.block_type = ONLY_LONG_WINDOW;
          psyInfo->pending.use_tns = 1;
      }
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
    psyInfo[channel].tns_active = 0;
    psyInfo[channel].current.block_type = ONLY_LONG_WINDOW;
    psyInfo[channel].current.use_tns = 0;
    psyInfo[channel].pending.block_type = ONLY_LONG_WINDOW;
    psyInfo[channel].pending.use_tns = 0;
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
              psyInfo[elem->channels[0]].pending.block_type = ONLY_LONG_WINDOW;
              psyInfo[elem->channels[0]].pending.use_tns = 0;
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

  /* block_type/use_tns were decided a frame early (PsyCalculate ran on the
     lookahead buffer), because MDCT needs to know the window shape before
     it can transform the frame that decision was made for. */
  psyInfo->current = psyInfo->pending;

  /* Shift the energy windows down by one frame: PREV<-CUR, CUR<-NEXT, freeing
     the NEXT region for the freshly-computed lookahead window below. */
  memmove(psydata->eng, psydata->eng + SUBBLOCKS_PER_FRAME,
          2 * SUBBLOCKS_PER_FRAME * sizeof(psyfloat));
  memmove(psydata->eng_low, psydata->eng_low + SUBBLOCKS_PER_FRAME,
          2 * SUBBLOCKS_PER_FRAME * sizeof(psyfloat));
  memmove(psydata->eng_high, psydata->eng_high + SUBBLOCKS_PER_FRAME,
          2 * SUBBLOCKS_PER_FRAME * sizeof(psyfloat));

  /* Assembly of the newest 2048-sample window for energy analysis */
  memcpy(transBuff, p_lookahead1, BLOCK_LEN_LONG * sizeof(float));
  memcpy(transBuff + BLOCK_LEN_LONG, p_lookahead2, BLOCK_LEN_LONG * sizeof(float));

  float alpha = expf(-2.0f * M_PI * 2000.0f / gpsyInfo->sampleRate);
  if (alpha < 0.0f) alpha = 0.0f;
  if (alpha >= 1.0f) alpha = 0.99f;

  for (win = 0; win < SUBBLOCKS_PER_FRAME; win++)
  {
    /* seg[-1] is in bounds (seg starts >= 448 samples in), so the first
     * difference carries across the sub-block boundary instead of resetting. */
    float *seg = transBuff + (win * BLOCK_LEN_SHORT) + (BLOCK_LEN_LONG - BLOCK_LEN_SHORT) / 2;
    float e = 0.0f;
    float e_low = 0.0f;
    float e_high = 0.0f;
    int l, n = 2 * psyInfo->sizeS;
    float lp_state = 0.0f;

    for (l = 0; l < n; l++)
    {
      float d = seg[l] - seg[l - 1];
      e += d * d;

      float lp = alpha * lp_state + (1.0f - alpha) * d;
      lp_state = lp;
      float hp = d - lp;
      e_low += lp * lp;
      e_high += hp * hp;
    }
    psydata->eng[ENG_WIN_NEXT + win] = (psyfloat)e;
    psydata->eng_low[ENG_WIN_NEXT + win] = (psyfloat)e_low;
    psydata->eng_high[ENG_WIN_NEXT + win] = (psyfloat)e_high;
  }
}

/* Reuses block-switching's sub-block energy (PREV..CUR) so TNS doesn't
 * re-derive the transient timeline for its filter direction. */
const float *PsyGetCurEnvelope(PsyInfo *psyInfo, int *len)
{
  psydata_t *psydata = (psydata_t *)psyInfo->data;
  if (len) *len = 2 * SUBBLOCKS_PER_FRAME;
  return (const float *)&psydata->eng_high[ENG_WIN_PREV];
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
              psyInfo[channel].pending.block_type = ONLY_SHORT_WINDOW;
          else
              psyInfo[channel].pending.block_type = ONLY_LONG_WINDOW;
      }
  }

  /* Use the same block type for all channels
     If there is 1 channel that wants a short block,
     use a short block on all channels.
   */
  for (channel = 0; channel < numChannels; channel++)
  {
    if (psyInfo[channel].pending.block_type == ONLY_SHORT_WINDOW)
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
    /* TNS reads current.block_type -- sync it to the coder's post-transition
       type, not the raw pending desire. */
    psyInfo[channel].current.block_type = coderInfo[channel].block_type;
  }
}
