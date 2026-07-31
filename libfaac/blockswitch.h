/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2001 Menno Bakker
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

#ifndef BLOCKSWITCH_H
#define BLOCKSWITCH_H


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "coder.h"
#include "channels.h"

struct faacEncStruct;

#define SUBBLOCKS_PER_FRAME 8
#define ENG_WIN_PREV (0 * SUBBLOCKS_PER_FRAME)
#define ENG_WIN_CUR  (1 * SUBBLOCKS_PER_FRAME)
#define ENG_WIN_NEXT (2 * SUBBLOCKS_PER_FRAME)
#define PSY_TD_THRESH (0.5f)

typedef struct
{
  float eng[3 * SUBBLOCKS_PER_FRAME];
  float eng_low[3 * SUBBLOCKS_PER_FRAME];
  float eng_high[3 * SUBBLOCKS_PER_FRAME];
} psydata_t;

typedef struct {
	int size;
	int sizeS;

	int block_type;
	float td_hard;
	int tns_alert;
	int tns_active;

        void *data;
} PsyInfo;

typedef struct {
	float sampleRate;

	/* shared work buffers */
	float *sharedWorkBuffLong;  /* Used for 2048-sample windows (filtbank, psy, tns, mdct) */
} GlobalPsyInfo;

void PsySetTdHard(PsyInfo *psyInfo, unsigned int numChannels, int tnsActive, unsigned int sampleRate);
void PsyInit (GlobalPsyInfo *gpsyInfo, PsyInfo *psyInfo,
		unsigned int numChannels, unsigned int sampleRate);
void PsyEnd (PsyInfo *psyInfo, unsigned int numChannels);
void PsyCalculate (AACElement *elements, int numElements, PsyInfo *psyInfo,
		unsigned int numChannels);
void PsyBufferUpdate (GlobalPsyInfo * gpsyInfo, PsyInfo * psyInfo,
		float * restrict p_lookahead1,
		float * restrict p_lookahead2);
void BlockSwitch (struct faacEncStruct *hEncoder, CoderInfo *coderInfo, PsyInfo *psyInfo,
		unsigned int numChannels);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* BLOCKSWITCH_H */
