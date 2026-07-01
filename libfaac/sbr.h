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
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef SBR_H
#define SBR_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "bitstream.h"
#include "coder.h"
#include "faac_real.h"
#include "fft.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SBR_QMF_BANDS_64     64
#define SBR_QMF_OVL_LEN_64   576
#define SBR_MAX_BANDS        64
#define SBR_MAX_ENVELOPES     2
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

/* Transient detection threshold (peak-to-mean power ratio). */
#define SBR_TRANSIENT_THRESH_DEFAULT    ((faac_real)4.0)
/* div-by-zero guard for the peak/mean ratio in silence frames (~-150 dBFS^2). */
#define SBR_ENERGY_FLOOR                ((faac_real)1e-15)
/* log2(0) guard in envelope quantization: -200 dBFS^2, below all SBR quantizer ranges. */
#define SBR_LOG_ENERGY_FLOOR            ((faac_real)1e-20)
/* Default noise floor level (ISO 14496-3 §4.6.18.6.4). */
#define SBR_NOISE_LEVEL_DEFAULT         4
/* 6 = log2(64): normalises 64-band QMF energy to per-band level. ISO 14496-3 §4.6.18.6.3. */
#define SBR_ENV_LEVEL_LOG2_OFFSET       ((faac_real)6.0)
/* Rate-dependent resolution thresholds. */
#define SBR_AMP_RES_BITRATE_BPS         20000u
#define SBR_COARSE_TABLE_BITRATE_BPS    32000u
/* sbr_offset table row for Single-rate SBR (Mode 0, ISO 14496-3:2009 Table 4.87). */
#define SBR_OFFSET_ROW_SINGLE_RATE      6

/* Per-channel SBR state. Everything indexed [ch] in SBRInfo lives here. */
typedef struct SBRChannel {
    faac_real qmfOvl64[SBR_QMF_OVL_LEN_64]; /* QMF overlap state (carries across frames) */
    int envData  [SBR_MAX_ENVELOPES][SBR_MAX_BANDS]; /* quantised envelope indices */
    int noiseData[SBR_MAX_NOISE_ENVELOPES][SBR_MAX_NOISE_BANDS]; /* quantised noise floor indices */
    int invfMode;                                    /* bs_invf_mode (0–3) */
} SBRChannel;

typedef struct SBRInfo {
    int sbrPresent;
    int headerSent;
    int frameCount;
    int numChannels;
    int sampleRate;        /* full output rate; the core runs at sampleRate/2 (dual-rate) or Fs (single-rate) */
    int singleRate;        /* 1 = HE core at full rate (rare), 0 = dual-rate (Fs/2 core) */

    /* --- frequency band configuration (set at init, constant per stream) --- */
    int kx;
    int k2;
    int dk;                /* master frequency table step (1 or 2 QMF bands) */
    int numBands;
    int bandEdges[SBR_MAX_BANDS + 1];
    int numNoiseBands;

    /* --- bitstream header fields --- */
    int bs_amp_res;
    int bs_freq_res;       /* envelope frequency resolution: 1 = HIGH (f_master) */
    int bs_start_freq;
    int bs_stop_freq;
    int bs_xover_band;
    int bs_alter_scale;

    /* --- per-frame state --- */
    int numEnvelopes;      /* 1 or 2, set by transient detection in SbrEncode */
    int eff_amp_res;       /* forced to 0 for single-envelope FIXFIX (ISO 14496-3:2009 §4.6.18.3) */

    /* Envelope time grid for the frame (frame-global, shared by both channels of
     * a CPE). frameClass selects FIXFIX or VARFIX; tEnv[0..numEnvelopes] are the
     * envelope borders in SBR time slots ([0, SBR_NUM_TIME_SLOTS]) and are only
     * emitted for the variable classes. bsPointer marks the transient envelope. */
    int frameClass;
    int tEnv[SBR_MAX_ENVELOPES + 1];
    int bsPointer;

    /* Cached SBR fill-element payload. */
    int payloadValid;
    int payloadBits;          /* ext_type + flag + header + data */
    int cachedSendHeader;
    unsigned char payloadBuf[1024];

    /* --- per-channel state --- */
    SBRChannel ch[MAX_CHANNELS];

    /* QMF analysis twiddle factors. */
    faac_real twidCos[SBR_QMF_BANDS_64];
    faac_real twidSin[SBR_QMF_BANDS_64];
    faac_real oddCos [SBR_QMF_BANDS_64];
    faac_real oddSin [SBR_QMF_BANDS_64];
    FFT_Tables *fftTables;   /* borrowed: the encoder's shared core FFT tables */
} SBRInfo;

struct SignalAnalysis;

SBRInfo *SbrInit(int channels, int sampleRate, unsigned long bitRate, int singleRate, FFT_Tables *fft_tables);
/* Recompute the bitrate/single-rate-dependent band config without reallocating;
 * lets SetConfiguration toggle sub-mode on an existing handle. */
void SbrUpdate(SBRInfo *sbr, unsigned long bitRate, int singleRate);
void SbrEnd(SBRInfo *sbr);

void SbrQmfAnalysis(SBRInfo *sbr, const faac_real * restrict ovl_pos, faac_real * restrict energy, int kx, int k2);
void SbrEncode(SBRInfo *sbr, faac_real *timeDomain[MAX_CHANNELS], int numChannels, int numSamples, struct SignalAnalysis *sa);
int SbrWrite(SBRInfo *sbr, BitStream *bs, int id_aac, int writeFlag, struct SignalAnalysis *sa);

#ifdef __cplusplus
}
#endif

#endif
