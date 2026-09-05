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

#ifndef FRAME_H
#define FRAME_H

/* Input sample FIFO slots, each one frame (FRAME_LEN samples) wide, relative to
   the frame currently being coded (FIFO_CURR): one frame behind (FIFO_PAST,
   reused as the MDCT overlap) and two frames ahead. The two ahead slots are
   needed because the block-switch energy analysis works on 2-frame-wide windows
   and keeps one window of lookahead, whose far edge reaches two frames ahead. */
#define LOOKAHEAD_DEPTH 2
#define FIFO_PAST       0
#define FIFO_CURR       1
#define FIFO_AHEAD1     2
#define FIFO_AHEAD2     3

#include "faac_internal.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "coder.h"
#include "channels.h"
#include "blockswitch.h"
#include "fft.h"
#include "quantize.h"
#include "sbr.h"
#include "stats.h"

typedef struct faacEncStruct {
    /* number of channels in AAC file */
    unsigned int numChannels;

    /* samplerate of AAC file */
    unsigned long sampleRate;
    unsigned int sampleRateIdx;

    unsigned int usedBytes;

    /* frame number */
    unsigned int frameNum;
    unsigned int flushFrame;

    /* Scalefactorband data */
    SR_INFO *srInfo;

    /* sample buffers: FIFO_PAST (MDCT overlap), FIFO_CURR, FIFO_AHEAD1, FIFO_AHEAD2 */
    float *audioFIFO[MAX_CHANNELS][4];

    /* Filterbank buffers */
    float *sin_window_long;
    float *sin_window_short;
    float *kbd_window_long;
    float *kbd_window_short;
    float *freqBuff[MAX_CHANNELS];

    /* Channel and Coder data for all channels */
    CoderInfo coderInfo[MAX_CHANNELS];

    /* Element-centric configuration */
    AACElement elements[MAX_CHANNELS];
    int numElements;
    bool isLfeChannel[MAX_CHANNELS]; /* per-channel LFE lookup, derived from elements[] whenever it changes */

    /* Psychoacoustics data */
    PsyInfo psyInfo[MAX_CHANNELS];
    GlobalPsyInfo gpsyInfo;

    /* Configuration data */
    faacEncConfiguration config;

    /* quantizer specific config */
    AACQuantCfg aacquantCfg;

    /* FFT Tables */
    FFT_Tables	fft_tables;

    /* Input FIFO: decouples the caller's per-call chunk size from the encoder
     * frame size. faacEncEncode appends whatever it is handed (any count) and
     * emits one frame once a full frame (mult*FRAME_LEN samples/ch, mult = 2 for
     * HE-AAC, 1 for LC) has accumulated. Stores format-converted float. */
    float    *inputFifo[MAX_CHANNELS];
    unsigned int  inputFifoFill;     /* samples per channel currently buffered */
    unsigned int  inputFifoCap;      /* per-channel capacity in samples */

    /* faac_* API: AudioSpecificConfig cached on first request and owned by the
     * handle (freed at close), so faac_encoder_asc() can hand back a pointer
     * the caller never frees. NULL until first built. */
    unsigned char *ascCache;
    unsigned long  ascCacheLen;

    /* HE-AAC / SBR state */
    struct SBRContext *sbrContext;   /* SBR analysis state and bitstream data */

    /* Peak-limiter retry scratch: one buffer per channel holding book[] at
     * [0] and sf[] at [MAX_SCFAC_BANDS]. */
    int *peakSnap[MAX_CHANNELS];

    /* Rate-control bit account. NOT an AAC bit reservoir -- see the note on
       RC_BALANCE_AIM in frame.c. */
    int bitBalance;         /* signed: banked bits positive, owed bits negative */
    int sbrBitsAcc;         /* EWMA of the SBR payload, scaled by 1<<RC_SBR_EWMA_SHIFT */

    /* The bit reservoir proper: a model of the decoder's input buffer,
       AAC_MAX_BITS_PER_CH per channel, refilled by resMean bits per frame and
       drained one frame at a time. resFill is what it holds after the last
       frame, in [0, resCap]; it caps the next frame in faacEncEncode and is
       declared in the ADTS header. resMean == 0 is VBR: nothing is declared or
       enforced. resFill < 0 means no frame has been seen yet. */
    int resMean;
    int resCap;
    int resFill;

    /* What the previous frame cost and the quality it was coded at, so the
       controller can tell a frame that is under budget because quality is too
       low from one that is under budget because there is nothing to spend on. */
    int rcPrevBits;
    float rcPrevQual;
    /* Fewest bits this frame may carry without the reservoir overflowing;
       BuildFrame stuffs a fill element up to it. 0 when nothing is required. */
    int resMinBits;
    int stuffedBits;   /* what BuildFrame stuffed into the last frame */
} faacEncStruct;

/* Reservoir fill once a frame of payloadBits (raw_data_block, no ADTS header)
   has gone out. Shared by the ADTS header and the rate controller so the two
   cannot disagree. */
int faacReservoirAfter(const faacEncStruct *hEncoder, int payloadBits);

/* Configuration worker behind faac_encoder_open(): validates the config,
 * resolves AUTO/HE-AAC, and (re)initializes the encoder. Returns 1 on success,
 * 0 on failure. */
int faacEncApplyConfig(faacEncStruct* hEncoder,
                       faacEncConfigurationPtr config);

/* Samples/channel per full frame: HE-AAC's core runs dual-rate at Fs/2, so it
 * needs two FRAME_LENs of input to emit one frame at the full rate; LC needs one. */
static inline unsigned int faacFrameSamples(const faacEncStruct *hEncoder)
{
    return (hEncoder->config.aacObjectType == HE_V1) ? 2 * FRAME_LEN : FRAME_LEN;
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FRAME_H */
