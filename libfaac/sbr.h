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

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* HE-AAC v1 Spectral Band Replication (SBR) encoder, ISO/IEC 14496-3:2009 §4.6.18 */

#ifndef SBR_H
#define SBR_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "faac_real.h"
#include "coder.h"
#include "fft.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SBR_QMF_BANDS_64     64
#define SBR_QMF_OVL_LEN_64   576
#define SBR_MAX_BANDS        64
#define SBR_MAX_ENVELOPES     2
#define SBR_MAX_NOISE_BANDS   5
/* Re-transmit header every 30 frames (~0.7 s): lets decoders seek to any frame. */
#define SBR_HEADER_PERIOD    30

#define SBR_FRAME_CLASS_FIXFIX  0

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
/* 6 = log2(64): normalises 64-band QMF energy to per-band level. ISO 14496-3 §4.6.18.6.3. */
#define SBR_ENV_LEVEL_LOG2_OFFSET       ((faac_real)6.0)
/* Below 20 kbps/ch, 1.5 dB envelope resolution (bs_amp_res=0) recovers ~0.3 dB MOS;
 * above it Huffman savings from 3 dB steps outweigh precision. */
#define SBR_AMP_RES_BITRATE_BPS         20000u
/* Below 32 kbps/ch use dk=2/alter_scale=1 for fewer bands and lower overhead.
 * Net-positive on ViSQOL at ≤32 kbps; above it finer resolution is worth the bits. */
#define SBR_COARSE_TABLE_BITRATE_BPS    32000u

/* Per-channel SBR state. Everything indexed [ch] in SBRInfo lives here. */
typedef struct SBRChannel {
    faac_real qmfOvl64[SBR_QMF_OVL_LEN_64]; /* QMF overlap state (carries across frames) */
    int envData  [SBR_MAX_ENVELOPES][SBR_MAX_BANDS]; /* quantised envelope indices */
    int noiseData[SBR_MAX_NOISE_BANDS];              /* quantised noise floor indices */
    int invfMode;                                    /* bs_invf_mode (0–3) */
} SBRChannel;

typedef struct SBRInfo {
    int sbrPresent;
    int headerSent;
    int frameCount;
    int numChannels;
    int sampleRate;        /* full output rate; the core runs at sampleRate/2 */

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
    int numEnvelopes;      /* 1 or 2, set by transient detection in SBRAnalysis */
    faac_real transientThresh; /* peak/mean slot power ratio; see SBR_TRANSIENT_THRESH_DEFAULT */
    int eff_amp_res;       /* forced to 0 for single-envelope FIXFIX (ISO 14496-3:2009 §4.6.18.3) */

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

SBRInfo *SBRInit(int channels, int sampleRate, unsigned long bitRate, FFT_Tables *fft_tables);
void SBREnd(SBRInfo *sbr);
void SBRAnalysis(SBRInfo *sbr, faac_real *timeDomain[MAX_CHANNELS], int numChannels, int numSamples);
#include "bitstream.h"
int SBRWriteBitstream(SBRInfo *sbr, BitStream *bs, int id_aac, int writeFlag);

#ifdef __cplusplus
}
#endif

#endif
