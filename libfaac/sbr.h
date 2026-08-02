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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "coder.h"
#include "fft.h"
#include "sbr_analysis.h"

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

/* SBR_QMF_BANDS_64, SBR_QMF_OVL_LEN_64 and SBR_MAX_ENVELOPES come from
 * sbr_analysis.h (included above), which sizes its arrays with them. */
#define SBR_MAX_BANDS        64
#define SBR_MAX_NOISE_ENVELOPES 2
#define SBR_MAX_NOISE_BANDS   5
#define SBR_HEADER_PERIOD    30

/* SBR frame classes (ISO 14496-3:2009 §4.6.18.3, Table 4.80). */
#define SBR_FRAME_CLASS_FIXFIX  0
#define SBR_FRAME_CLASS_FIXVAR  1
#define SBR_FRAME_CLASS_VARFIX  2
#define SBR_FRAME_CLASS_VARVAR  3

/* Envelope time-slot resolution the decoder uses for an AAC-LC core frame
 * (FFmpeg/FAAD2 pass numTimeSlots=16). All bs_rel_bord/t_env values written in
 * a variable grid live in [0, SBR_NUM_TIME_SLOTS]. */
#define SBR_NUM_TIME_SLOTS   16

/* SBR extension types (ISO 14496-3 §4.6.18). */
#define SBR_EXT_TYPE_SBR     0xd
#define SBR_EXT_TYPE_SBR_CRC 0xe
/* bs_extension_id inside an SBR payload's extended data (§8.6.4). */
#define SBR_EXT_ID_PS        0x2

/* Transient detection threshold (peak-to-mean power ratio). */
#define SBR_TRANSIENT_THRESH_DEFAULT    (4.0f)
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

/* --- HE-AAC v2 parametric stereo (ISO 14496-3 §8.6.4) --- */
/* Parameter bands. iid_mode/icc_mode 0 is the 10-band "coarse" resolution: the
 * cheapest legal mode, and the only one that stays affordable at 24-56 kbps. */
#define SBR_PS_BANDS                    10
/* IID quantizer: 15 levels, index -7..+7 (Table 8.24, default resolution). */
#define SBR_PS_IID_LEVELS               15
/* ICC quantizer: 8 levels, index 0..7 (Table 8.26). */
#define SBR_PS_ICC_LEVELS               8
/* Highest ICC index this encoder will transmit.
 *
 * The downmix carries one broadband compensation gain, but the decoder undoes
 * it per band. For a centred image the two cancel exactly -- it reconstructs
 * sqrt((1+icc)/2) of the mono sum and the gain below is sqrt(2/(1+icc)) -- so
 * the problem is not the algebra, it is the range. Measured end to end through
 * the decoder, the fraction of the mono sum each index preserves is:
 *
 *   index   0     1     2     3     4     5     6     7
 *   kept  1.00  0.98  0.96  0.89  0.83  0.71  0.45  0.002
 *
 * Indices 6 and 7 describe antiphase channels and need 2.2x and 500x to undo;
 * SBR_PS_MAX_DOWNMIX_GAIN caps the gain at 2.0, so those bands lose most of
 * their energy from the very downmix the core spent its bits coding -- and the
 * mono downmix is what a monaural quality metric scores. Stopping at 3 keeps
 * the required compensation inside 1.00-1.12, a span one broadband gain can
 * actually represent, and costs only the widest decorrelation.
 *
 * Chosen by measuring the whole curve (10 clips, ViSQOL on the mono downmix vs
 * phase 3 coherence error, both against HE-AAC v1 from the same build). Index 3
 * is the knee: MOS beats v1 at every rate tested (+0.030 at 12k, +0.073 at 16k,
 * +0.028 at 24k) while the stereo image still beats it at 12k and 16k. Index 2
 * buys another 0.004 MOS and gives up that lead. */
#define SBR_PS_ICC_MAX_INDEX            3
/* Ceiling on the energy-preserving downmix gain (+6 dB). Unbounded, it diverges
 * on near-antiphase material, where the sum downmix approaches silence. */
#define SBR_PS_MAX_DOWNMIX_GAIN         (2.0f)
/* One-pole coefficient for the downmix gain; low enough that the gain rides
 * stereo-width changes instead of pumping on them. */
#define SBR_PS_GAIN_SMOOTH              (0.25f)

typedef struct SBRInfo SBRInfo;
struct SignalAnalysis;

typedef struct SBRContext SBRContext;

SBRContext *SbrContextInit(int channels);
void SbrContextEnd(SBRContext *sbrCtx);
int SbrContextGetASC(SBRContext *sbrCtx, int coreSRIdx, int channels, unsigned char** ppBuffer, unsigned long* pSize, int aacObjectType);
unsigned int SbrContextGetXOverBandwidth(SBRContext *sbrCtx);
float SbrContextGetDownmixGain(SBRContext *sbrCtx);
void SbrContextUpdateConfig(SBRContext *sCtx, int channels, unsigned long bitrate, FFT_Tables *fft_tables, int aacObjectType);
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
