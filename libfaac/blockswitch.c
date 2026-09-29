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
#include "core_inject.h"

typedef float psyfloat;

/* The high-pass energy timeline is held as one contiguous array of per-sub-block
   energies rather than separate prev/curr/next arrays, so the +-2 sub-block
   lookahead around the current frame is a single sliding index instead of
   three-way stitching.
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
  /* Bit i set: the energy step into sub-block i is a transient. Judged once,
     when the sub-block's energy is produced, so the per-frame decision is a
     mask test rather than a re-walk of the timeline. */
  unsigned attack;
  psyfloat level; /* running level of the sub-block energies so far */
}
psydata_t;

/* The high-pass first difference (d[n]=x[n]-x[n-1]) de-weights bass, whose
 * broadband energy would otherwise mask HF attacks and false-trigger short
 * blocks on stationary music; what's left tracks the band where pre-echo is
 * audible. A sub-block whose energy leaves [level/ratio, level*ratio] of the
 * running level before it is a transient. On LC the level spans roughly the
 * last three sub-blocks, so dense stationary texture stops tripping short
 * windows while onsets and the drop-outs after them still do. The bit-starved
 * HE core gains from more short windows than its attacks alone call for, so
 * it judges against the neighbouring sub-block alone, with a tighter band. */
#define PSY_LEVEL_RATIO_LC  (2.5f)
#define PSY_LEVEL_SMOOTH_LC (0.3f)
#define PSY_LEVEL_RATIO_HE  (1.5f)

/* Probe-only knobs (window-decision study). Unset = production behaviour.
 *   FAAC_BS_RATIO     override levelRatio (rise test)
 *   FAAC_BS_DROPRATIO ratio for the drop-out test (0 disables drop-outs)
 *   FAAC_BS_SMOOTH    override levelSmooth
 *   FAAC_BS_MINE      absolute sub-block energy floor for a rise to count
 *   FAAC_BS_PREVS / FAAC_BS_NEXTS  sub-blocks of context around the frame
 *   FAAC_BS_NOHYST    1: no extra short frame after a short-desiring frame
 *   FAAC_BS_RESET     b>0: after a rise, lift the level to at least b*e (level recovery)
 *   FAAC_BS_DUMP      per-frame detector state to this file */
static float bs_env(const char *name, float def)
{
  const char *v = getenv(name);
  return (v && *v) ? (float)atof(v) : def;
}
static int bs_knobs_init;
static float bs_dropratio = -1.0f, bs_mine, bs_prevs = 2, bs_nexts = 2, bs_nohyst, bs_reset;
static FILE *bs_dump;
static void bs_knobs(void)
{
  if (bs_knobs_init) return;
  bs_knobs_init = 1;
  bs_dropratio = bs_env("FAAC_BS_DROPRATIO", -1.0f);
  bs_mine = bs_env("FAAC_BS_MINE", 0.0f);
  bs_prevs = bs_env("FAAC_BS_PREVS", 2);
  bs_nexts = bs_env("FAAC_BS_NEXTS", 2);
  bs_nohyst = bs_env("FAAC_BS_NOHYST", 0);
  bs_reset = bs_env("FAAC_BS_RESET", 0);
  if (getenv("FAAC_BS_DUMP")) bs_dump = fopen(getenv("FAAC_BS_DUMP"), "w");
}

/* Attack anywhere in the frame or its immediate temporal context, sub-blocks
   [cur-2, cur+9], wants a short block. */
static void PsyCheckShort(PsyInfo * psyInfo)
{
  const psydata_t *psydata = (const psydata_t *)psyInfo->data;
  int PREVS = (int)bs_prevs, NEXTS = (int)bs_nexts;
  unsigned span = (1u << (PREVS + SUBBLOCKS_PER_FRAME + NEXTS - 1)) - 1;

  psyInfo->block_type = (psydata->attack >> (ENG_WIN_CUR - PREVS + 1)) & span
                        ? ONLY_SHORT_WINDOW : ONLY_LONG_WINDOW;
}

void PsyInit(GlobalPsyInfo * gpsyInfo, PsyInfo * psyInfo, unsigned int numChannels,
		    unsigned int sampleRate, bool heCore)
{
  unsigned int channel;
  int size;

  gpsyInfo->sampleRate = (float) sampleRate;
  gpsyInfo->levelRatio = heCore ? PSY_LEVEL_RATIO_HE : PSY_LEVEL_RATIO_LC;
  gpsyInfo->levelSmooth = heCore ? 1.0f : PSY_LEVEL_SMOOTH_LC;
  bs_knobs();
  gpsyInfo->levelRatio = bs_env("FAAC_BS_RATIO", gpsyInfo->levelRatio);
  gpsyInfo->levelSmooth = bs_env("FAAC_BS_SMOOTH", gpsyInfo->levelSmooth);

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
float PsyGetAttack(PsyInfo * psyInfo)
{
  psydata_t *psydata = (psydata_t *)psyInfo->data;
  float strength = 0.0f, total = 0.0f;
  int win;

  if (!psydata)
    return 0.0f;

  for (win = 0; win < SUBBLOCKS_PER_FRAME; win++)
  {
    float e = (float)psydata->eng[ENG_WIN_PREV + win];

    total += e;
    if (win)
    {
      float p = (float)psydata->eng[ENG_WIN_PREV + win - 1];
      float lo = (e < p) ? e : p;
      float s = fabsf(e - p) / lo;      /* IEEE divide covers silence */

      if (s > strength) strength = s;
    }
  }

  return total > 0.0f ? strength : 0.0f;
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
void PsyCalculate(PsyInfo * psyInfo, const bool * isLfeChannel,
			 unsigned int numChannels)
{
  for (unsigned int channel = 0; channel < numChannels; channel++)
  {
      if (isLfeChannel[channel])
          psyInfo[channel].block_type = ONLY_LONG_WINDOW;
      else
          PsyCheckShort(&psyInfo[channel]);
  }
}

void PsyBufferUpdate(GlobalPsyInfo * gpsyInfo, PsyInfo * psyInfo,
                            float * restrict p_lookahead1,
                            float * restrict p_lookahead2)
{
  int win;
  float * restrict transBuff = gpsyInfo->sharedWorkBuffLong;
  psydata_t *psydata = (psydata_t *)psyInfo->data;
  float level = psydata->level;

  /* Shift the energy windows down by one frame: PREV<-CUR, CUR<-NEXT, freeing
     the NEXT region for the freshly-computed lookahead window below. */
  memmove(psydata->eng, psydata->eng + SUBBLOCKS_PER_FRAME,
          2 * SUBBLOCKS_PER_FRAME * sizeof(psyfloat));
  psydata->attack >>= SUBBLOCKS_PER_FRAME;

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
    {
      float dr = bs_dropratio < 0.0f ? gpsyInfo->levelRatio : bs_dropratio;
      int rise = e > gpsyInfo->levelRatio * level && e >= bs_mine;
      if (rise || (dr > 0.0f && e * dr < level))
        psydata->attack |= 1u << (ENG_WIN_NEXT + win);
      level = gpsyInfo->levelSmooth * e + (1.0f - gpsyInfo->levelSmooth) * level;
      if (rise && bs_reset > 0.0f && level < bs_reset * e)
        level = bs_reset * e;
    }
  }
  psydata->level = level;
}

void BlockSwitch(CoderInfo * coderInfo, PsyInfo * psyInfo, unsigned int numChannels)
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

  /* Probe-only (core_inject.c): fdk's own window_sequence for this frame,
   * looked up once (every channel shares one ciFrame value, set in frame.c
   * before this call). Applied per channel below, AFTER the natural decision,
   * as a post-hoc override rather than by pre-empting `desire` -- `desire`
   * only ever takes the natural psychoacoustic value here, so the hysteresis
   * read of desired_block_type two lines down stays driven by real
   * psychoacoustic history even on a frame the override later replaces. */
  int have_donor_win = 0, donor_win_seq = 0;
  {
    struct CoreInject *cinj = CoreInjectGet();
    if (cinj && (CoreInjectFields(cinj) & CI_WIN))
    {
      for (channel = 0; channel < numChannels; channel++)
      {
        if (coderInfo[channel].ciCh == 0)
        {
          have_donor_win = CoreInjectLookupWin(cinj, coderInfo[channel].ciFrame, 0,
                                                &donor_win_seq, NULL, NULL, NULL);
          break;
        }
      }
    }
  }

  for (channel = 0; channel < numChannels; channel++)
  {
    int lasttype = coderInfo[channel].block_type;

    if (desire == ONLY_SHORT_WINDOW
	|| (!bs_nohyst && coderInfo[channel].desired_block_type == ONLY_SHORT_WINDOW))
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
    if (bs_dump && psyInfo[channel].data)
    {
      const psydata_t *pd = (const psydata_t *)psyInfo[channel].data;
      fprintf(bs_dump, "B %d %u %d %d %d %u %g", coderInfo[channel].ciFrame, channel,
              psyInfo[channel].block_type, desire, coderInfo[channel].block_type,
              pd->attack, (double)pd->level);
      for (int w = 0; w < 3 * SUBBLOCKS_PER_FRAME; w++)
        fprintf(bs_dump, " %g", (double)pd->eng[w]);
      fputc('\n', bs_dump);
    }

    /* Probe-only (core_inject.c): swap in fdk's win_seq when it's a legal
     * continuation from `lasttype` (the same predecessor state the natural
     * decision above was just judged against). An illegal donor value (donor
     * disagreed with FAAC on the LAST frame's family, so a raw transplant
     * here would violate the LONG_START/STOP pairing) is silently declined,
     * leaving the natural decision in place -- same as "no donor record".
     * desired_block_type is overwritten to the override's own family so the
     * NEXT frame's hysteresis check above sees the overridden history. */
    if (have_donor_win)
    {
      int legal = (lasttype == ONLY_LONG_WINDOW || lasttype == SHORT_LONG_WINDOW)
                  ? (donor_win_seq == ONLY_LONG_WINDOW || donor_win_seq == LONG_SHORT_WINDOW)
                  : (donor_win_seq == ONLY_SHORT_WINDOW || donor_win_seq == SHORT_LONG_WINDOW);
      if (legal)
      {
        coderInfo[channel].block_type = donor_win_seq;
        coderInfo[channel].desired_block_type =
            (donor_win_seq == ONLY_SHORT_WINDOW || donor_win_seq == LONG_SHORT_WINDOW)
            ? ONLY_SHORT_WINDOW : ONLY_LONG_WINDOW;
      }
    }
  }
}
