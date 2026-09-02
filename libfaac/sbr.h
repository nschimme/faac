/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2026 Nils Schimmelmann
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

#ifndef SBR_H
#define SBR_H

#include "coder.h"
#include "fft.h"
#include "sbr_analysis.h"

#define LOOKAHEAD_DEPTH 2

/* Depth of the HE shared-detector decision FIFO. Sized so index 0 lines up with
   the core frame currently being coded: the core lags the freshest SBR analysis
   by LOOKAHEAD_DEPTH frames, so index 0 must be LOOKAHEAD_DEPTH entries behind
   the newest (one extra slot for the newest entry itself). */
#define SBR_DETECT_FIFO (LOOKAHEAD_DEPTH + 1)

/* Depth of the coded-SBR-payload delay ring. Envelopes must describe the audio
   the access unit actually carries, which is FIFO_PAST: the MDCT window spans
   (FIFO_PAST, FIFO_CURR) and, at 50% overlap, an access unit completes the first
   half of its own window. So the payload emitted on call N belongs to frame
   N-(LOOKAHEAD_DEPTH+1), while SbrEncode has just analysed frame N -- that delay,
   plus a slot for the newest entry.

   Note this is one deeper than SBR_DETECT_FIFO: block switching wants the frame
   *after* the coded one, so a transient gets a start window now and a short
   window next, whereas envelopes must land on the coded frame itself. */
#define SBR_FRAME_FIFO (LOOKAHEAD_DEPTH + 2)

/* SBR codes exactly one element, an SCE or a CPE, so the payload never spans
   more than two channels regardless of the core's channel count. */
#define SBR_MAX_CODED_CHANNELS 2

#ifdef __cplusplus
extern "C" {
#endif

struct BitStream;

#define SBR_QMF_BANDS_64     64
/* Group delay of the half-band decimator in resample.c, in full-rate samples.
   The FIR is symmetric and RESAMPLE_FILTER_LEN (63) taps long, so its centre
   sits at tap 31 and core output sample i carries full-rate time 2*i - 31. */
#define SBR_RESAMPLE_DELAY   31
/* QMF history, one whole slot more than the 576 the 640-tap prototype strictly
   needs. Keeping it a multiple of the slot size holds the per-frame frame copy
   on a 64-float boundary; the odd sample offset lives in the read pointer below,
   where it is free -- the QMF reads are backwards stride-2 gathers, not
   contiguous loads. */
#define SBR_QMF_OVL_LEN_64   640
/* Slot s reads from here, so its window ends SBR_RESAMPLE_DELAY samples before
   the slot boundary. Without the shift the analysis sits on the undecimated time
   base while the core sits on the decimator's, and every envelope describes
   audio 0.7 ms (at 44.1 kHz) later than the core placed in that slot. */
#define SBR_QMF_READ_OFFSET  (SBR_QMF_BANDS_64 - SBR_RESAMPLE_DELAY)
/* Group delay of the 640-tap QMF analysis prototype, in QMF slots: 640/2 = 320
 * samples = 5 slots. Pass 1 finds the attack in the time domain, pass 2 measures
 * energy through the QMF, so a border derived from a pass-1 slot index has to be
 * pushed forward by this much or it lands ahead of the energy it is meant to
 * bracket -- leaving the transient envelope sitting on pre-attack silence. */
#define SBR_QMF_DELAY_SLOTS  5
#define SBR_MAX_BANDS        64
/* Pre-attack, attack, post-attack. The spec allows 5, but each slot costs an
 * envData row in every delay-ring entry, so bound it at what the grid emits. */
#define SBR_MAX_ENVELOPES     3
#define SBR_MAX_NOISE_ENVELOPES 2
#define SBR_MAX_NOISE_BANDS   1
#define SBR_HEADER_PERIOD    30

/* SBR frame classes (ISO 14496-3:2009 §4.6.18.3, Table 4.80). */
#define SBR_FRAME_CLASS_FIXFIX  0
#define SBR_FRAME_CLASS_FIXVAR  1
#define SBR_FRAME_CLASS_VARFIX  2

/* Envelope time-slot resolution the decoder uses for an AAC-LC core frame
 * (FFmpeg/FAAD2 pass numTimeSlots=16). All bs_rel_bord/t_env values written in
 * a variable grid live in [0, SBR_NUM_TIME_SLOTS]. */
#define SBR_NUM_TIME_SLOTS   16

/* SBR extension types (ISO 14496-3 §4.6.18). */
#define SBR_EXT_TYPE_SBR     0xd

/* Transient detection threshold: how far a QMF slot's power must rise above the
 * running average of the slots before it to count as an attack. 4.0 = 6 dB. */
#define SBR_TRANSIENT_THRESH_DEFAULT    (4.0f)
/* Weight of the newest slot in that running average: a ~2-slot (~2.9 ms at
 * 44.1 kHz) memory. Long enough to average over a slot of ringing, short enough
 * that the signal immediately before an attack does not dilute the rise the
 * detector is looking for. */
#define SBR_ATTACK_EMA_ALPHA            (0.125f)
/* div-by-zero guard for the peak/mean ratio in silence frames (~-150 dBFS^2). */
#define SBR_ENERGY_FLOOR                (1e-15f)
/* log2(0) guard in envelope quantization: -200 dBFS^2, below all SBR quantizer ranges. */
#define SBR_LOG_ENERGY_FLOOR            (1e-20f)
/* Default noise floor level (ISO 14496-3 §4.6.18.6.4). */
#define SBR_NOISE_LEVEL_DEFAULT         4
/* 6 = log2(64): normalises 64-band QMF energy to per-band level. ISO 14496-3 §4.6.18.6.3. */
#define SBR_ENV_LEVEL_LOG2_OFFSET       (6.0f)
/* Rate-dependent resolution thresholds. */
#define SBR_AMP_RES_BITRATE_BPS         20000u
#define SBR_COARSE_TABLE_BITRATE_BPS    32000u
/* Stop-frequency search bounds (bs_stop_freq). 13 is the largest worth
 * searching: it already pins k2 to its 64-band ceiling at every supported
 * rate, so higher indices would just signal more range for the same band. */
#define SBR_STOP_FREQ_MIN               10
#define SBR_STOP_FREQ_MAX               13
/* Where widening aims. Past this the bands are inaudible to essentially every
 * listener while costing exactly as much as the ones below. */
#define SBR_STOP_FREQ_TARGET_HZ         20000
/* Max delta-coded step for envelope data, per bs_amp_res grid (ISO 14496-3
 * §4.6.18.3.6): the fine (amp_res=1) grid has half the step size of the coarse
 * grid, so its delta range must be roughly double to cover the same dB span. */
#define SBR_ENV_DELTA_LIMIT_HIRES       31
#define SBR_ENV_DELTA_LIMIT_LORES       60

typedef struct SBRInfo SBRInfo;
struct SignalAnalysis;

typedef struct SBRContext SBRContext;

SBRContext *SbrContextInit(int channels);
void SbrContextEnd(SBRContext *sbrCtx);
int SbrContextGetASC(SBRContext *sbrCtx, int coreSRIdx, int channels, unsigned char** ppBuffer, unsigned long* pSize);
unsigned int SbrContextGetXOverBandwidth(SBRContext *sbrCtx);
void SbrContextUpdateConfig(SBRContext *sCtx, int channels, unsigned long bitrate, FFT_Tables *fft_tables);
void SbrContextProcessFrame(SBRContext *sCtx, int numChannels, int realPerCh, float *inputFifo[MAX_CHANNELS], float *heHalfRate[MAX_CHANNELS]);
int SbrContextIsPresent(SBRContext *sCtx);
void SbrContextRestoreRate(SBRContext *sCtx, unsigned long *sampleRate, unsigned int *sampleRateIdx, SR_INFO **srInfo);
unsigned long SbrContextGetFullRate(SBRContext *sCtx, unsigned long defaultRate);
void SbrContextResolveRate(SBRContext *sCtx, unsigned long *sampleRate, unsigned int *sampleRateIdx, SR_INFO **srInfo);
int SbrContextIsAnalysisValid(SBRContext *sCtx);
int SbrContextGetWantShort(SBRContext *sCtx, int channel, int index);

int SbrContextGetBits(SBRContext *sCtx, struct BitStream *bs, int channels, int aacObjectType, int writeFlag);

#ifdef __cplusplus
}
#endif

#endif
