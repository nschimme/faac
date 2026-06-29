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
/* Re-transmit header every 30 frames (~0.7 s): lets decoders seek to any frame. */
#define SBR_HEADER_PERIOD    30

/* SBR frame classes (ISO 14496-3:2009 §4.6.18.3, Table 4.80). FIXFIX uses
 * equal-spaced borders; the variable classes place borders explicitly. */
#define SBR_FRAME_CLASS_FIXFIX  0
#define SBR_FRAME_CLASS_FIXVAR  1
#define SBR_FRAME_CLASS_VARFIX  2
#define SBR_FRAME_CLASS_VARVAR  3

/* Envelope time-slot resolution the decoder uses for an AAC-LC core frame
 * (FFmpeg/FAAD2 pass numTimeSlots=16). All bs_rel_bord/t_env values written in
 * a variable grid live in [0, SBR_NUM_TIME_SLOTS]. */
#define SBR_NUM_TIME_SLOTS   16

/* EXT_SBR_DATA / EXT_SBR_DATA_CRC fill-element extension types (ISO 14496-3 §4.6.18) */
#define SBR_EXT_TYPE_SBR     0xd
#define SBR_EXT_TYPE_SBR_CRC 0xe

/* Peak-to-mean QMF slot power ratio that triggers 2-envelope (transient) framing;
 * 4 ≈ 6 dB. ViSQOL sweep {2,3,4,5,6,8} over percussive + tonal music: 4.0 is the
 * optimum. Lower over-triggers and regresses tonal clips (the equal-split second
 * envelope costs bits/precision); higher leaves percussive pre-echo unprotected.
 * The old 16 (≈ 12 dB) effectively never fired, so SBR always ran single-envelope. */
#define SBR_TRANSIENT_THRESH_DEFAULT    ((faac_real)4.0)
/* div-by-zero guard for the peak/mean ratio in silence frames (~-150 dBFS^2). */
#define SBR_ENERGY_FLOOR                ((faac_real)1e-15)
/* log2(0) guard in envelope quantization: -200 dBFS^2, below all SBR quantizer ranges. */
#define SBR_LOG_ENERGY_FLOOR            ((faac_real)1e-20)
/* Coded SBR noise-floor level; higher value = LOWER injected noise (ISO 14496-3
 * §4.6.18.6.4). The previous 0 injected the maximum noise floor, swamping the
 * highband. A full-corpus ViSQOL sweep nets positive as this rises, but a fixed
 * value is a content tradeoff (tonal clips want less noise, breathy/noisy ones
 * more): 4 is the safe floor (net +0.006, worst-case -0.06); 8+ breaches -0.25 on
 * noisy clips. A tonality-adaptive floor is the real fix (TODO). */
#define SBR_NOISE_LEVEL_DEFAULT         4
/* 6 = log2(64): normalises 64-band QMF energy to per-band level. ISO 14496-3 §4.6.18.6.3. */
#define SBR_ENV_LEVEL_LOG2_OFFSET       ((faac_real)6.0)
/* Below 20 kbps/ch, 1.5 dB envelope resolution (bs_amp_res=0) recovers ~0.3 dB MOS;
 * above it Huffman savings from 3 dB steps outweigh precision. */
#define SBR_AMP_RES_BITRATE_BPS         20000u
/* Below 32 kbps/ch use dk=2/alter_scale=1 for fewer bands and lower overhead.
 * Net-positive on ViSQOL at ≤32 kbps; above it finer resolution is worth the bits. */
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
    /* numEnvelopes/eff_amp_res are frame-global because the encoder emits one shared
     * FIXFIX grid for all channels. When the variable envelope grid lands (VARFIX/
     * FIXVAR + bs_rel_bord, see ../TODO), per-channel transients place their own
     * borders and these must move into SBRChannel. */
    int numEnvelopes;      /* 1 or 2, set by transient detection in SBRAnalysis */
    int eff_amp_res;       /* forced to 0 for single-envelope FIXFIX (ISO 14496-3:2009 §4.6.18.3) */

    /* Envelope time grid for the frame (frame-global, shared by both channels of
     * a CPE). frameClass selects FIXFIX or VARFIX; tEnv[0..numEnvelopes] are the
     * envelope borders in SBR time slots ([0, SBR_NUM_TIME_SLOTS]) and are only
     * emitted for the variable classes. bsPointer marks the transient envelope. */
    int frameClass;
    int tEnv[SBR_MAX_ENVELOPES + 1];
    int bsPointer;

    /* Cached SBR fill-element payload for the current frame. The payload length and
     * bytes are frozen once SBRAnalysis finishes, so emit it once and reuse it across
     * the rate-control / CountBitstream / WriteBitstream calls (the real write blits
     * payloadBuf instead of re-running the grid/Huffman emit). Invalidated once per
     * frame by SBRAnalysis and rebuilt on the next write; payloadBuf is sized to the
     * array-bound worst case (~690 bytes for a maxed CPE). See SBRWriteBitstream. */
    int payloadValid;         /* 0 until the payload is built for the current frame */
    int payloadBits;          /* ext_type + flag + header + data */
    int cachedSendHeader;
    unsigned char payloadBuf[1024];

    /* --- per-channel state --- */
    SBRChannel ch[MAX_CHANNELS];

    /* --- QMF analysis tables (init once, borrowed FFT tables) ---
     * twidCos/Sin pre-rotate the even/odd-packed window output;
     * oddCos/Sin recombine the two real-subsequence DFTs. */
    faac_real twidCos[SBR_QMF_BANDS_64];
    faac_real twidSin[SBR_QMF_BANDS_64];
    faac_real oddCos [SBR_QMF_BANDS_64];
    faac_real oddSin [SBR_QMF_BANDS_64];
    FFT_Tables *fftTables;   /* borrowed: the encoder's shared core FFT tables */
} SBRInfo;

struct SignalAnalysis;

SBRInfo *SBRInit(int channels, int sampleRate, unsigned long bitRate, int singleRate, FFT_Tables *fft_tables);
/* Recompute the bitrate/single-rate-dependent band config without reallocating;
 * lets SetConfiguration toggle sub-mode on an existing handle. */
void SBRUpdate(SBRInfo *sbr, unsigned long bitRate, int singleRate);
void SBREnd(SBRInfo *sbr);

void qmf_analysis_64_slot_energy_fft(SBRInfo *sbr, const faac_real * restrict ovl_pos, faac_real * restrict energy, int kx, int k2);
void SBRAnalysis(SBRInfo *sbr, faac_real *timeDomain[MAX_CHANNELS], int numChannels, int numSamples, struct SignalAnalysis *sa);
int SBRWriteBitstream(SBRInfo *sbr, BitStream *bs, int id_aac, int writeFlag, struct SignalAnalysis *sa);

#ifdef __cplusplus
}
#endif

#endif
