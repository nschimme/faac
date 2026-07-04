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
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "blockswitch.h"
#include "coder.h"
#include "util.h"
#include <faac.h>

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

/* The high-pass first difference (d[n]=x[n]-x[n-1]) de-weights bass, whose
 * broadband energy would otherwise mask HF attacks and false-trigger short
 * blocks on stationary music; what's left tracks the band where pre-echo is
 * audible. A relative energy jump between sub-blocks past this threshold is a
 * transient. */
#define PSY_TD_THRESH ((faac_real)0.5)

/* Hard-transient ceiling when TNS is active: transients with strength in
   (PSY_TD_THRESH, PSY_TD_HARD] may stay in long windows with TNS covering
   the pre-echo. Setting it equal to PSY_TD_THRESH empties the band (legacy
   behavior). 2.0 chosen by a zimtohrli sweep over 0.7/1.0/1.5/2.0/4.0 at
   20/40/64 kbps: +0.031 MOS at 20k (95% CI excludes 0), neutral above,
   speech +0.09 at 16k; ~15 % of short frames become long. */
#define PSY_TD_HARD ((faac_real)2.0)

/* Promotion trades pre-echo for smearing quantization noise across the long
   window, and that smear is audible in *milliseconds* while the window is
   fixed in *samples*: 1024 samples is 21 ms at 48 kHz but 64 ms at 16 kHz --
   most of a syllable, well past temporal masking. PSY_TD_HARD was tuned on
   48 kHz material where the trade wins; at 16 kHz the same threshold tripled
   the temporal damage and ViSQOL showed a systematic speech regression
   (~200 clips one direction, worst -0.61 MOS). So don't retune the strength
   threshold for low rates -- the cost side of the trade scales with window
   duration, hence a sample-rate floor. 32 kHz keeps the window at <= 32 ms. */
#define PSY_TD_HARD_MIN_SR 32000

void PsySetTdHard(PsyInfo *psyInfo, unsigned int numChannels, int tnsActive,
                  unsigned int sampleRate)
{
  faac_real hard = PSY_TD_THRESH;
  unsigned int channel;

  if (tnsActive && sampleRate >= PSY_TD_HARD_MIN_SR)
    hard = PSY_TD_HARD;

  for (channel = 0; channel < numChannels; channel++)
    psyInfo[channel].td_hard = hard;
}

static void PsyCheckShort(PsyInfo * psyInfo)
{
  enum {PREVS = 2, NEXTS = 2};
  psydata_t *psydata = (psydata_t *)psyInfo->data;
  int win;
  faac_real lasteng = (faac_real)psydata->eng[ENG_WIN_CUR - PREVS]; /* start at PREVS before current */
  faac_real strength = 0.0;

  psyInfo->block_type = ONLY_LONG_WINDOW;

  /* This analysis leads the frame being MDCT-encoded by one frame (its
     envelope sits in the PREV window; see PsyGetCurEnvelope), so the
     promotion flag consumed by TNS this frame is last call's decision. */
  psyInfo->promoted_long = psyInfo->promoted_pending;
  psyInfo->promoted_pending = 0;

  /* Search for transients across the current frame and its immediate temporal context.
     The search range is [curr-2, curr+9]. Track the strongest relative energy
     jump; the short/long+TNS decision below needs the maximum, not the first. */
  for (win = 1; win < PREVS + SUBBLOCKS_PER_FRAME + NEXTS; win++)
  {
      faac_real eng = (faac_real)psydata->eng[ENG_WIN_CUR - PREVS + win];

      faac_real toteng = (eng < lasteng) ? eng : lasteng;
      faac_real volchg = FAAC_FABS(eng - lasteng);

      /* Relative energy jump indicates a transient. IEEE divide handles silence cases. */
      faac_real s = volchg / toteng;
      if (s > strength)
          strength = s;
      lasteng = eng;
  }

  if (strength <= PSY_TD_THRESH)
      return;                            /* stationary: long */

  if (strength <= psyInfo->td_hard)
  {
      psyInfo->promoted_pending = 1;
      return;                            /* borderline: stay long; the long
                                            window's masking (and TNS when its
                                            gates agree) absorbs the pre-echo.
                                            An envelope-based "will TNS fire?"
                                            predictor here measured strictly
                                            worse than unconditional
                                            promotion. */
  }

  psyInfo->block_type = ONLY_SHORT_WINDOW;
}

static void PsyInit(GlobalPsyInfo * gpsyInfo, PsyInfo * psyInfo, unsigned int numChannels,
		    unsigned int sampleRate)
{
  unsigned int channel;
  int size;

  gpsyInfo->sampleRate = (faac_real) sampleRate;

  for (channel = 0; channel < numChannels; channel++)
  {
    psydata_t *psydata = (psydata_t *)AllocMemory(sizeof(psydata_t));
    if (!psydata) return;
    memset(psydata, 0, sizeof(psydata_t));
    psyInfo[channel].data = psydata;
  }

  size = BLOCK_LEN_LONG;
  for (channel = 0; channel < numChannels; channel++)
  {
    psyInfo[channel].size = size;
    psyInfo[channel].td_hard = PSY_TD_THRESH; /* empty band until PsySetTdHard */
    psyInfo[channel].promoted_long = 0;
    psyInfo[channel].promoted_pending = 0;
  }

  size = BLOCK_LEN_SHORT;
  for (channel = 0; channel < numChannels; channel++)
    psyInfo[channel].sizeS = size;
}

static void PsyEnd(PsyInfo * psyInfo, unsigned int numChannels)
{
  unsigned int channel;

  for (channel = 0; channel < numChannels; channel++)
  {
    if (psyInfo[channel].data)
      FreeMemory(psyInfo[channel].data);
  }
}

/* Do psychoacoustical analysis */
static void PsyCalculate(ChannelInfo * channelInfo, PsyInfo * psyInfo,
			 unsigned int numChannels
			)
{
  unsigned int channel;

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
                            faac_real * restrict p_lookahead1,
                            faac_real * restrict p_lookahead2)
{
  int win;
  faac_real * restrict transBuff = gpsyInfo->sharedWorkBuffLong;
  psydata_t *psydata = (psydata_t *)psyInfo->data;

  /* Shift the energy windows down by one frame: PREV<-CUR, CUR<-NEXT, freeing
     the NEXT region for the freshly-computed lookahead window below. */
  memmove(psydata->eng, psydata->eng + SUBBLOCKS_PER_FRAME,
          2 * SUBBLOCKS_PER_FRAME * sizeof(psyfloat));

  /* Assembly of the newest 2048-sample window for energy analysis */
  memcpy(transBuff, p_lookahead1, BLOCK_LEN_LONG * sizeof(faac_real));
  memcpy(transBuff + BLOCK_LEN_LONG, p_lookahead2, BLOCK_LEN_LONG * sizeof(faac_real));

  for (win = 0; win < SUBBLOCKS_PER_FRAME; win++)
  {
    /* seg[-1] is in bounds (seg starts >= 448 samples in), so the first
     * difference carries across the sub-block boundary instead of resetting. */
    faac_real *seg = transBuff + (win * BLOCK_LEN_SHORT) + (BLOCK_LEN_LONG - BLOCK_LEN_SHORT) / 2;
    faac_real e = 0.0;
    int l, n = 2 * psyInfo->sizeS;

    for (l = 0; l < n; l++)
    {
      faac_real d = seg[l] - seg[l - 1];
      e += d * d;
    }
    psydata->eng[ENG_WIN_NEXT + win] = (psyfloat)e;
  }
}

const float *PsyGetCurEnvelope(PsyInfo *psyInfo, int *len)
{
  psydata_t *psydata = (psydata_t *)psyInfo->data;
  if (len)
    *len = SUBBLOCKS_PER_FRAME;
  /* Due to the LOOKAHEAD_DEPTH=2 and the timing of PsyBufferUpdate, the
     time-domain envelope for the frame currently being MDCT-encoded resides
     in the PREV window. */
  return (const float *)&psydata->eng[ENG_WIN_PREV];
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
