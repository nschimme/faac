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
}
psydata_t;

/* Relative energy jump threshold for transient detection at 48 kHz. Sub-block
 * energy is computed on the first difference d[n]=x[n]-x[n-1], which
 * de-weights bass (whose broadband energy would otherwise mask HF attacks
 * and false-trigger short blocks on stationary music); what's left tracks
 * the band where pre-echo is audible. A jump above this threshold marks a
 * short-block candidate. */
#define PSY_TD_THRESH_48K (0.5f)

/* Lowest allowed base threshold. Protects PsyTdThresh against returning 0
 * or negative values on pathologically low sample rates. */
#define PSY_TD_THRESH_MIN (0.1f)

/* PsyTdThresh scales the 48 kHz base threshold inversely with sample rate:
 * lower sample rates have fewer samples per sub-block (and per frame), so
 * the energy calculation averages over a shorter temporal window where noise
 * spikes look larger relative to steady-state signal. Without scaling, a
 * 16 kHz stream triggers short blocks on ordinary consonants that stay in
 * long blocks at 48 kHz. */
static float PsyTdThresh(float sampleRate)
{
    float t;

    if (sampleRate <= 0.0f)
        return PSY_TD_THRESH_48K;

    t = PSY_TD_THRESH_48K * (48000.0f / sampleRate);
    if (t < PSY_TD_THRESH_MIN)
        t = PSY_TD_THRESH_MIN;
    return t;
}

#define PSY_TD_HARD_LOW      (2.5f)
#define PSY_TD_HARD_HIGH     (1.5f)
#define PSY_TD_HARD_LOW_BPS  (16000)
#define PSY_TD_HARD_HIGH_BPS (64000)

static float PsyTdHard(unsigned long bitratePerCh)
{
    float t;

    if (bitratePerCh == 0 || bitratePerCh >= PSY_TD_HARD_HIGH_BPS)
        return PSY_TD_HARD_HIGH;
    if (bitratePerCh <= PSY_TD_HARD_LOW_BPS)
        return PSY_TD_HARD_LOW;

    t = (float)(bitratePerCh - PSY_TD_HARD_LOW_BPS) /
        (float)(PSY_TD_HARD_HIGH_BPS - PSY_TD_HARD_LOW_BPS);
    return PSY_TD_HARD_LOW + t * (PSY_TD_HARD_HIGH - PSY_TD_HARD_LOW);
}

#define PSY_TD_HARD_MIN_SR 32000

static void PsyCheckShort(PsyInfo * psyInfo)
{
  enum {PREVS = 2, NEXTS = 2};
  psydata_t *psydata = (psydata_t *)psyInfo->data;
  int win;
  float lasteng = (float)psydata->eng[ENG_WIN_CUR - PREVS]; /* start at PREVS before current */
  float strength = 0.0f;
  float thresh = psyInfo->td_thresh;

  /* Search for transients across the current frame and its immediate temporal context.
     The search range is [curr-2, curr+9]. Track the strongest relative energy
     jump rather than stopping at the first crossing: BlockSwitch's joint
     short-block/TNS decision needs the maximum to compare against a second,
     higher threshold. */
  for (win = 1; win < PREVS + SUBBLOCKS_PER_FRAME + NEXTS; win++)
  {
      float eng = (float)psydata->eng[ENG_WIN_CUR - PREVS + win];

      float toteng = (eng < lasteng) ? eng : lasteng;
      float volchg = fabsf(eng - lasteng);

      /* Relative energy jump indicates a transient. IEEE divide handles silence cases. */
      float s = volchg / toteng;

      if (s > strength)
          strength = s;
      lasteng = eng;
  }

  psyInfo->td_strength = strength;
  psyInfo->block_type = (strength > thresh) ? ONLY_SHORT_WINDOW : ONLY_LONG_WINDOW;
}

void PsyInit(GlobalPsyInfo * gpsyInfo, PsyInfo * psyInfo, unsigned int numChannels,
		    unsigned int sampleRate)
{
  unsigned int channel;
  int size;
  float thresh;

  gpsyInfo->sampleRate = (float) sampleRate;

  for (channel = 0; channel < numChannels; channel++)
  {
    psydata_t *psydata = (psydata_t *)AllocMemory(sizeof(psydata_t));
    if (!psydata) return;
    memset(psydata, 0, sizeof(psydata_t));
    psyInfo[channel].data = psydata;
  }

  size = BLOCK_LEN_LONG;
  thresh = PsyTdThresh(gpsyInfo->sampleRate);
  for (channel = 0; channel < numChannels; channel++)
  {
    psyInfo[channel].size = size;
    psyInfo[channel].td_strength = 0.0f;
    psyInfo[channel].td_thresh = thresh;
  }

  size = BLOCK_LEN_SHORT;
  for (channel = 0; channel < numChannels; channel++)
    psyInfo[channel].sizeS = size;
}

/* Strongest relative energy jump across the sub-blocks of the window the MDCT
   is about to transform. ENG_WIN_PREV is exactly that window -- (FIFO_PAST,
   FIFO_CURR) -- because PsyBufferUpdate has already shifted by the time TNS
   runs.

   Exposed so TNS can gate on the temporal envelope already sitting in
   psydata instead of recomputing it. Returns 0 if PsyBufferUpdate hasn't
   populated the energy windows for this channel yet -- callers must treat
   that as "no basis to judge", not "flat". */
/* Peak-over-mean sub-block energy across the sub-blocks of the window the
   MDCT is about to transform. ENG_WIN_PREV is exactly that window --
   (FIFO_PAST, FIFO_CURR) -- because PsyBufferUpdate has already shifted by
   the time TNS runs.

   Exposed so TNS can gate on the temporal envelope already sitting in
   psydata instead of recomputing it: a frame with no sub-block energy spike
   has no attack for TNS's noise buildup to hide behind, so the LPC work
   isn't worth its bit cost -- see TnsAttackAdmits in frame.c. Returns 0 if
   PsyBufferUpdate hasn't populated the energy windows for this channel yet
   -- callers must treat that as "no basis to judge", not "flat". */
/* Single unified pass over ENG_WIN_PREV energy sub-blocks for PeakGate and Attack statistics. */
static void PsyGetEnvelopeStats(PsyInfo * psyInfo, float *out_peak_gate, float *out_attack)
{
  psydata_t *psydata = (psydata_t *)psyInfo->data;
  float peak = 0.0f, total = 0.0f, strength = 0.0f;
  int win;

  if (!psydata) {
    if (out_peak_gate) *out_peak_gate = 0.0f;
    if (out_attack) *out_attack = 0.0f;
    return;
  }

  for (win = 0; win < SUBBLOCKS_PER_FRAME; win++)
  {
    float e = (float)psydata->eng[ENG_WIN_PREV + win];
    total += e;
    if (e > peak) peak = e;

    if (win > 0) {
      float p = (float)psydata->eng[ENG_WIN_PREV + win - 1];
      float lo = (e < p) ? e : p;
      float s = fabsf(e - p) / lo;
      if (s > strength) strength = s;
    }
  }

  if (out_peak_gate)
    *out_peak_gate = (total > 0.0f) ? (peak / (total / (float)SUBBLOCKS_PER_FRAME)) : 0.0f;
  if (out_attack)
    *out_attack = (total > 0.0f) ? strength : 0.0f;
}

float PsyGetPeakGate(PsyInfo * psyInfo)
{
  float peak_gate;
  PsyGetEnvelopeStats(psyInfo, &peak_gate, NULL);
  return peak_gate;
}

float PsyGetAttack(PsyInfo * psyInfo)
{
  float attack;
  PsyGetEnvelopeStats(psyInfo, NULL, &attack);
  return attack;
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
/* Fast energy-based Perceptual Entropy approximation: sum subblock high-pass energies
   pre-computed in PsyBufferUpdate(), scaling by PE_ENERGY_SCALE to match PE complexity threshold. */
static void PsyCalcPE(PsyInfo * psyInfo)
{
  psydata_t *psydata = (psydata_t *)psyInfo->data;
  if (!psydata) { psyInfo->pe = 0.0f; return; }
  float pe = (float)psydata->eng[ENG_WIN_CUR + 0] + (float)psydata->eng[ENG_WIN_CUR + 1] +
             (float)psydata->eng[ENG_WIN_CUR + 2] + (float)psydata->eng[ENG_WIN_CUR + 3] +
             (float)psydata->eng[ENG_WIN_CUR + 4] + (float)psydata->eng[ENG_WIN_CUR + 5] +
             (float)psydata->eng[ENG_WIN_CUR + 6] + (float)psydata->eng[ENG_WIN_CUR + 7];
  psyInfo->pe = pe * PE_ENERGY_SCALE;
}

static void PsyAnalyzeChannel(PsyInfo * psyInfo)
{
  PsyCheckShort(psyInfo);
  PsyCalcPE(psyInfo);
}

/* Do psychoacoustical analysis */
void PsyCalculate(AACElement * elements, int numElements, PsyInfo * psyInfo,
			 unsigned int numChannels
			)
{
  if (elements == NULL) {
      for (unsigned int channel = 0; channel < numChannels; channel++)
          PsyAnalyzeChannel(&psyInfo[channel]);
      return;
  }

  for (int e = 0; e < numElements; e++)
  {
      AACElement *elem = &elements[e];
      switch (elem->type) {
          case ID_SCE:
              PsyAnalyzeChannel(&psyInfo[elem->channels[0]]);
              break;
          case ID_CPE:
              PsyAnalyzeChannel(&psyInfo[elem->channels[0]]);
              PsyAnalyzeChannel(&psyInfo[elem->channels[1]]);
              break;
          case ID_LFE:
              psyInfo[elem->channels[0]].block_type = ONLY_LONG_WINDOW;
              psyInfo[elem->channels[0]].pe = 0.0f;
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

  /* Shift the energy windows down by one frame: PREV<-CUR, CUR<-NEXT, freeing
     the NEXT region for the freshly-computed lookahead window below. */
  memmove(psydata->eng, psydata->eng + SUBBLOCKS_PER_FRAME,
          2 * SUBBLOCKS_PER_FRAME * sizeof(psyfloat));

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

    for (l = 0; l < n; l++)
    {
      float d = seg[l] - seg[l - 1];
      e += d * d;
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
  else
  {
      /* Joint short-block/TNS decision: a borderline transient (strength in
       * (td_thresh, td_hard]) stays in a long window instead of forcing
       * a short one, trusting long-window masking -- and TNS, when it's
       * active and its own gates agree -- to absorb the pre-echo. Only
       * applies on the core-psy path above (HE-AAC's SBR-driven override
       * doesn't compute td_strength). */

      /* FAAC_TD_THRESH is a process-wide debug knob, not per-encoder state,
       * so it's safe (and much cheaper than a getenv+atof every frame) to
       * parse it once per process and cache the result. 0.0f means "unset or
       * invalid" -- never a legal override, since valid values are always
       * >= the lowest possible base threshold. */
      static float env_td_hard = -1.0f;
      if (env_td_hard < 0.0f) {
          const char *env_hard = getenv("FAAC_TD_THRESH");
          float e = env_hard ? (float)atof(env_hard) : 0.0f;
          env_td_hard = (e >= PSY_TD_THRESH_MIN) ? e : 0.0f;
      }

      for (channel = 0; channel < numChannels; channel++)
      {
          float td_hard = psyInfo[channel].td_thresh;

          if (env_td_hard > 0.0f) {
              td_hard = env_td_hard;
          } else if (hEncoder->config.useTns && hEncoder->sampleRate >= PSY_TD_HARD_MIN_SR) {
              td_hard = PsyTdHard(hEncoder->config.bitRate);
          }

          if (psyInfo[channel].block_type == ONLY_SHORT_WINDOW
              && psyInfo[channel].td_strength <= td_hard)
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
