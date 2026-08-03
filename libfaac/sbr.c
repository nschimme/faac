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

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include "sbr.h"
#include "sbr_tables.h"
#include "util.h"
#include "sbr_analysis.h"
#include "resample.h"
#include "bitstream.h"
#include "sbr_internal.h"
#include "faac_internal.h"

/* SBR master frequency band table (ISO/IEC 14496-3:2005 §4.6.18.3.2). kx/k2 are
 * spec-mandatory: the decoder reconstructs them from the sample rate alone, so
 * these must match its table exactly or the envelope band count desyncs. The
 * rate here is the full output rate (= 2*core), which is what the decoder uses. */

/* SBR start frequency (kx). Crossover alignment prevents aliasing/gaps. */
static int compute_kx(int sampleRate, int bs_start_freq)
{
    int temp = (sampleRate < 32000) ? 3000 : (sampleRate < 64000) ? 4000 : 5000;
    int start_min = ((temp << 7) + (sampleRate >> 1)) / sampleRate;
    int row = (sampleRate <= 16000) ? 0 : (sampleRate <= 22050) ? 1 : (sampleRate <= 24000) ? 2 : (sampleRate <= 32000) ? 3 : (sampleRate <= 64000) ? 4 : 5;
    return clamp_int(start_min + sbr_offset[row][bs_start_freq & 15], 1, 63);
}

static int cmp_int16(const void *a, const void *b) { return (int)(*(const short *)a) - (int)(*(const short *)b); }

/* SBR stop frequency (k2). Bark-scale distribution maximizes bit efficiency. */
static int compute_k2(int sampleRate, int kx, int bs_stop_freq)
{
    if (bs_stop_freq == 14 || bs_stop_freq == 15) return 64;
    int temp = (sampleRate < 32000) ? 3000 : (sampleRate < 64000) ? 4000 : 5000;
    int stop_min = ((temp << 8) + (sampleRate >> 1)) / sampleRate;
    int k2;
    if (bs_stop_freq < 14) {
        short stop_dk[13];
        float prod = (float)stop_min;
        int prev = stop_min;
        float base = powf(64.0f / (float)stop_min, (float)(1.0f / 13.0f));
        for (int i = 0; i < 12; i++) {
            prod *= base;
            int present = (int)lrintf(prod);
            stop_dk[i] = (short)(present - prev);
            prev = present;
        }
        stop_dk[12] = (short)(64 - prev);
        qsort(stop_dk, 13, sizeof(short), cmp_int16);
        k2 = stop_min;
        for (int i = 0; i < bs_stop_freq; i++) k2 += stop_dk[i];
    } else {
        k2 = 64;
    }

    int max_span = (sampleRate <= 32000) ? 48 : (sampleRate <= 44100) ? 35 : 32;
    return clamp_int(k2, kx + 1, kx + max_span > 64 ? 64 : kx + max_span);
}

/* Smallest stop-frequency index reaching targetHz, or the largest useful one.
 * Searched rather than tabulated: the index-to-frequency mapping comes from
 * compute_k2 and shifts with sample rate, so a fixed table would overshoot
 * at some rates. */
static int pick_stop_freq(int sampleRate, int kx, int targetHz)
{
    for (int sf = SBR_STOP_FREQ_MIN; sf < SBR_STOP_FREQ_MAX; sf++)
        if ((long)compute_k2(sampleRate, kx, sf) * sampleRate / (2 * SBR_QMF_BANDS_64) >= targetHz)
            return sf;
    return SBR_STOP_FREQ_MAX;
}

/* Distribute QMF bands into SBR master bands using uniform dk-spacing.
 * Residual bands are merged into the first/last pairs to maintain a
 * monotonic frequency grid. */
static int build_freq_table(SBRInfo *sbr)
{
    int kx = sbr->kx, k2 = sbr->k2, dk = sbr->dk;
    int n_master = clamp_int(((k2 - kx + (dk & 2)) >> dk) << 1, 1, SBR_MAX_BANDS);
    int f_master[SBR_MAX_BANDS + 1];
    for (int k = 1; k <= n_master; k++) f_master[k] = dk;
    int k2diff = (k2 - kx) - n_master * dk;
    if (k2diff < 0) {
        f_master[1]--;
        if (k2diff < -1) f_master[2]--;
    } else if (k2diff > 0) f_master[n_master]++;
    f_master[0] = kx;
    for (int k = 1; k <= n_master; k++) f_master[k] += f_master[k - 1];
    sbr->numBands = n_master;
    for (int b = 0; b <= n_master; b++) sbr->bandEdges[b] = f_master[b];
    sbr->numNoiseBands = 1;
    return n_master;
}

SBRInfo *SbrInit(int channels, int sampleRate, unsigned long bitRate, FFT_Tables *fft_tables)
{
    SBRInfo *sbr = (SBRInfo *)AllocMemory(sizeof(SBRInfo));
    if (!sbr) return NULL;
    SetMemory(sbr, 0, sizeof(SBRInfo));
    sbr->sbrPresent = 1;
    sbr->numChannels = channels;
    sbr->sampleRate = sampleRate;
    /* Unity until the first HE-AAC v2 frame measures the downmix; the zero the
     * memset leaves would otherwise silence the first core frame. */
    sbr->downmixGain = 1.0f;

    /* Pre-calculate twiddle factors for the FFT-based QMF analysis.
     * These coefficients rotate the subband indices into the odd-frequency
     * DFT space required by the SBR modulation kernel. */
    for (int m = 0; m < SBR_QMF_BANDS_64; m++) {
        sbr->twidCos[m] = (float)cos(M_PI_DOUBLE * m / 64.0);
        sbr->twidSin[m] = (float)sin(M_PI_DOUBLE * m / 64.0);
        sbr->oddCos[m] = (float)cos(M_PI_DOUBLE * (2 * m + 1) / 128.0);
        sbr->oddSin[m] = (float)sin(M_PI_DOUBLE * (2 * m + 1) / 128.0);
    }
    /* Borrow the encoder's shared core FFT tables (same fft() routine, same
     * logm=6 size as the short-block MDCT). The core owns init/terminate; the
     * logm=6 table is built lazily on first use, single-threaded per encoder. */
    sbr->fftTables = fft_tables;

    SbrUpdate(sbr, bitRate);
    return sbr;
}

/* Re-resolve SBR operational parameters (crossover, resolution) when the
 * bitrate or sample rate changes, avoiding handle reallocation. */
void SbrUpdate(SBRInfo *sbr, unsigned long bitRate)
{
    int sampleRate = sbr->sampleRate;
    unsigned long rate_per_ch = bitRate / sbr->numChannels;
    sbr->bs_amp_res = (rate_per_ch < SBR_AMP_RES_BITRATE_BPS) ? 0 : 1;
    /* Target crossover near the core ceiling (~11.6 kHz) maximizes MOS.
     * Higher-order parametric reconstruction below 10 kHz is audible and
     * generally inferior to the bit-starved LC core. */
    if (rate_per_ch <= SBR_COARSE_TABLE_BITRATE_BPS) {
        sbr->bs_start_freq = 15;
        sbr->bs_alter_scale = 1;
        sbr->dk = 2;
    } else {
        sbr->bs_start_freq = 15;
        sbr->bs_alter_scale = 0;
        sbr->dk = 1;
    }
    sbr->bs_freq_res = 1; /* HIGH resolution */
    sbr->bs_xover_band = 0; /* every master band is an SBR band; no low-res split */
    sbr->kx = compute_kx(sampleRate, sbr->bs_start_freq);

    /* Where the reconstruction stops. Aim at hearing rather than k2's ceiling:
     * bands above the target cost the same envelope bits as the ones below, so
     * there's no reason to stop short of what's audible. */
    sbr->bs_stop_freq = pick_stop_freq(sampleRate, sbr->kx, SBR_STOP_FREQ_TARGET_HZ);
    sbr->k2 = compute_k2(sampleRate, sbr->kx, sbr->bs_stop_freq);

    build_freq_table(sbr);
}

void SbrEnd(SBRInfo *sbr)
{
    if (!sbr) return;
    /* fftTables is borrowed from the encoder; the core terminates it. */
    FreeMemory(sbr);
}

/* What analysing a silent frame yields. Needed because zeroed memory is not a
 * legal payload: numEnvelopes == 0 encodes no grid at all. */
static void sbr_frame_silence(SbrFrameData *fd)
{
    SetMemory(fd, 0, sizeof(*fd));
    fd->numEnvelopes = 1;
    fd->eff_amp_res  = 0;
    fd->frameClass   = SBR_FRAME_CLASS_FIXFIX;
    fd->tEnv[0]      = 0;
    fd->tEnv[1]      = SBR_NUM_TIME_SLOTS;
    fd->bsPointer    = 0;
    for (int ch = 0; ch < SBR_MAX_CODED_CHANNELS; ch++) {
        fd->ch[ch].invfMode = 3;
        for (int ne = 0; ne < SBR_MAX_NOISE_ENVELOPES; ne++)
            fd->ch[ch].noiseData[ne][0] = SBR_NOISE_LEVEL_DEFAULT;
    }
}

SBRContext *SbrContextInit(int channels)
{
    SBRContext *sbrCtx = (SBRContext *)AllocMemory(sizeof(SBRContext));
    if (sbrCtx) {
        SetMemory(sbrCtx, 0, sizeof(SBRContext));
        sbrCtx->resampler = ResampleInit(channels);
        if (!sbrCtx->resampler) {
            FreeMemory(sbrCtx);
            return NULL;
        }
        /* The first access units carry the core's silent lead-in, so the ring
         * has to start full of payloads that describe silence. */
        for (int i = 0; i < SBR_FRAME_FIFO; i++)
            sbr_frame_silence(&sbrCtx->frameFIFO[i]);
    }
    return sbrCtx;
}

void SbrContextEnd(SBRContext *sbrCtx)
{
    if (!sbrCtx) return;
    if (sbrCtx->sbrInfo) {
        SbrEnd(sbrCtx->sbrInfo);
    }
    if (sbrCtx->resampler) {
        ResampleEnd(sbrCtx->resampler);
    }
    FreeMemory(sbrCtx);
}

int SbrContextGetASC(SBRContext *sbrCtx, int coreSRIdx, int channels, unsigned char** ppBuffer, unsigned long* pSize, int aacObjectType)
{
    /* Backward-compatible explicit ASC: the core is always signalled as plain
     * AAC-LC, and SBR (and for HE-AAC v2, PS) ride along in sync extensions a
     * plain-LC decoder skips. The core rate is Fs/2 (dual-rate SBR); the
     * extension declares the full output rate.
     *
     * Object type 29 must NOT appear as the core type here. The 5-bit type is
     * read as the *core* codec, and AOT 29 selects the hierarchical form, in
     * which the very next field is the extension rate -- so a decoder reading it
     * would consume the 0x2b7 sync as a rate index and desync.
     *
     * 49 bits for HE-AAC v2, 37 for v1; 7 bytes covers the larger. The reported
     * size is taken from where the writer actually stopped, so it stays right if
     * the field list changes. */
    const unsigned long cap = 7;
    *ppBuffer = (unsigned char *)malloc(cap);
    if (*ppBuffer == NULL) return -3;
    memset(*ppBuffer, 0, cap);
    BitStream *pBitStream = OpenBitStream(cap, *ppBuffer);
    PutBit(pBitStream, LOW,                         5);  /* core object type = 2 */
    PutBit(pBitStream, coreSRIdx,                   4);  /* core rate (Fs/2, dual-rate) */
    PutBit(pBitStream, channels,                    4);  /* core channels (1 for HE-AAC v2) */
    PutBit(pBitStream, 0, 1);                            /* frameLengthFlag */
    PutBit(pBitStream, 0, 1);                            /* dependsOnCoreCoder */
    PutBit(pBitStream, 0, 1);                            /* extensionFlag */
    PutBit(pBitStream, 0x2b7,                      11);  /* syncExtensionType */
    PutBit(pBitStream, HE_V1,                       5);  /* extObjectType = SBR */
    PutBit(pBitStream, 1,                           1);  /* sbrPresentFlag */
    PutBit(pBitStream, sbrCtx->fullSampleRateIdx,   4);  /* SBR output rate (2*core) */
    if (IsHEV2(aacObjectType)) {
        /* psPresentFlag is gated behind its own 11-bit sync, not appended bare
         * to the SBR extension (ISO 14496-3 1.6.2.1). */
        PutBit(pBitStream, 0x548,                  11);  /* syncExtensionType (PS) */
        PutBit(pBitStream, 1,                       1);  /* psPresentFlag */
    }
    *pSize = (pBitStream->currentBit + 7) / 8;
    CloseBitStream(pBitStream);
    return 0;
}

unsigned int SbrContextGetXOverBandwidth(SBRContext *sbrCtx)
{
    if (!sbrCtx || !sbrCtx->sbrInfo) return 0;
    /* kx * Fs / (2*64): each QMF band is Fs/(2*SBR_QMF_BANDS_64) Hz wide.
     * Matching core bandwidth to the SBR crossover avoids a gap or overlap. */
    return (unsigned int)((sbrCtx->sbrInfo->kx * sbrCtx->fullSampleRate) /
                           (2 * SBR_QMF_BANDS_64));
}

/* Gain the HE-AAC v2 caller must apply to its 0.5*(L+R) downmix so the mono core
 * carries the energy the PS decoder will assume it does. Valid after SbrEncode
 * has run for the frame being downmixed. */
float SbrContextGetDownmixGain(SBRContext *sCtx)
{
    return (sCtx && sCtx->sbrInfo) ? sCtx->sbrInfo->downmixGain : 1.0f;
}

void SbrContextUpdateConfig(SBRContext *sCtx, int channels, unsigned long bitrate, FFT_Tables *fft_tables, int aacObjectType)
{
    if (!sCtx) return;
    if (!sCtx->sbrInfo)
        sCtx->sbrInfo = SbrInit(channels, sCtx->fullSampleRate, bitrate, fft_tables);
    else
        SbrUpdate(sCtx->sbrInfo, bitrate);
    if (sCtx->sbrInfo)
        sCtx->sbrInfo->is_he_v2 = (IsHEV2(aacObjectType));
}

void SbrContextProcessFrame(SBRContext *sCtx, int numChannels, int realPerCh, float *inputFifo[MAX_CHANNELS], float *heHalfRate[MAX_CHANNELS])
{
    unsigned int channel;
    Resampler *rs = sCtx->resampler;
    float *fullPtrs[MAX_CHANNELS];

    /* SbrEncode quantizes into the new head; SbrWrite (via SbrContextGetBits)
     * emits the oldest slot, which is the payload for this frame's core audio. */
    sCtx->frameHead = (sCtx->frameHead + 1) % SBR_FRAME_FIFO;
    SbrFrameData *fd = &sCtx->frameFIFO[sCtx->frameHead];

    /* Flush frames are silence, whose analysis result is known up front: no
     * transient, one FIXFIX envelope, floored levels, default noise floors.
     * Skip straight to it -- the core signal is substituted with silence in
     * frame.c anyway -- but keep the delay lines advancing so the payloads
     * still in flight drain out. */
    if (realPerCh == 0) {
        sbr_frame_silence(fd);
        for (channel = 0; channel < (unsigned int)numChannels; channel++) {
            memmove(&sCtx->transientStrengthFIFO[channel][0], &sCtx->transientStrengthFIFO[channel][1], (SBR_DETECT_FIFO - 1) * sizeof(float));
            sCtx->transientStrengthFIFO[channel][SBR_DETECT_FIFO - 1] = 0.0f;
            memmove(&sCtx->wantShortFIFO[channel][0], &sCtx->wantShortFIFO[channel][1], (SBR_DETECT_FIFO - 1) * sizeof(int));
            sCtx->wantShortFIFO[channel][SBR_DETECT_FIFO - 1] = 0;
            heHalfRate[channel] = rs->halfRate[channel];
        }
        return;
    }

    for (channel = 0; channel < (unsigned int)numChannels; channel++) {
        float *fullRate = rs->fullRate[channel];
        fullPtrs[channel] = fullRate;
        memcpy(fullRate, inputFifo[channel], realPerCh * sizeof(float));
        /* Final partial frame: silence-pad the unfilled full-rate tail to
         * prevent the resampler from consuming stale data. */
        if (realPerCh < 2 * FRAME_LEN)
            memset(fullRate + realPerCh, 0, (2 * FRAME_LEN - realPerCh) * sizeof(float));
        heHalfRate[channel] = rs->halfRate[channel];
    }

    /* Always the full padded frame, never [0, realPerCh): the grid unconditionally
     * claims SBR_NUM_TIME_SLOTS, so normalising a short frame over fewer slots
     * would inflate its levels, and the QMF-overlap save below reads the last
     * SBR_QMF_OVL_LEN_64 samples -- behind the buffer for a short frame. */
    SbrAnalyze(&sCtx->signalAnalysis, fullPtrs, numChannels, 2 * FRAME_LEN, sCtx->sbrInfo);

    /* Update the transient FIFO. Shift down by one and push
     * the newest decision at SBR_DETECT_FIFO-1; index 0 stays aligned with the
     * core frame being coded (LOOKAHEAD_DEPTH frames behind this analysis). */
    for (channel = 0; channel < (unsigned int)numChannels; channel++) {
        memmove(&sCtx->transientStrengthFIFO[channel][0], &sCtx->transientStrengthFIFO[channel][1], (SBR_DETECT_FIFO - 1) * sizeof(float));
        sCtx->transientStrengthFIFO[channel][SBR_DETECT_FIFO - 1] = sCtx->signalAnalysis.ch[channel].transientStrength;
        memmove(&sCtx->wantShortFIFO[channel][0], &sCtx->wantShortFIFO[channel][1], (SBR_DETECT_FIFO - 1) * sizeof(int));
        sCtx->wantShortFIFO[channel][SBR_DETECT_FIFO - 1] = sCtx->signalAnalysis.ch[channel].wantShort;
    }

    SbrEncode(sCtx->sbrInfo, fullPtrs, numChannels, 2 * FRAME_LEN, &sCtx->signalAnalysis, fd);

    /* Dual-rate decimation: produces the halved-rate core signal. */
    Resample(rs, 2 * FRAME_LEN);
}

void SbrContextRestoreRate(SBRContext *sCtx, unsigned long *sampleRate, unsigned int *sampleRateIdx, SR_INFO **srInfoPtr)
{
    if (sCtx && sCtx->fullSampleRate > 0) {
        *sampleRate    = sCtx->fullSampleRate;
        *sampleRateIdx = sCtx->fullSampleRateIdx;
        *srInfoPtr     = &srInfo[*sampleRateIdx];
        sCtx->fullSampleRate = 0;
    }
}

unsigned long SbrContextGetFullRate(SBRContext *sCtx, unsigned long defaultRate)
{
    return (sCtx && sCtx->fullSampleRate) ? sCtx->fullSampleRate : defaultRate;
}

/* Dual-rate SBR: the AAC core encodes at Fs/2 while SBR reconstructs the top
 * octave back to the full rate. Halve the core rate here; the full rate is kept
 * in the context for SBR and the ASC. */
void SbrContextResolveRate(SBRContext *sCtx, unsigned long *sampleRate, unsigned int *sampleRateIdx, SR_INFO **srInfoPtr)
{
    if (sCtx->fullSampleRate == 0) {
        sCtx->fullSampleRate     = *sampleRate;
        sCtx->fullSampleRateIdx  = *sampleRateIdx;
        *sampleRate         = *sampleRate / 2;
        *sampleRateIdx      = GetSRIndex(*sampleRate);
        *srInfoPtr          = &srInfo[*sampleRateIdx];
    }
}

int SbrContextIsAnalysisValid(SBRContext *sCtx)
{
    return sCtx ? sCtx->signalAnalysis.valid : 0;
}

int SbrContextGetWantShort(SBRContext *sCtx, int channel, int index)
{
    if (sCtx && channel < MAX_CHANNELS && index < SBR_DETECT_FIFO) {
        return sCtx->wantShortFIFO[channel][index];
    }
    return 0;
}

int SbrContextIsPresent(SBRContext *sCtx)
{
    return (sCtx && sCtx->sbrInfo) ? 1 : 0;
}

/* Optimized log2 approximation for energy-to-decibel conversion.
 * Precision is sufficient for the 1.5/3.0 dB envelope quantizer. */
#define FAST_LOG2_A         1.3424f
#define FAST_LOG2_B         0.3427f
#define FAST_LOG2_MANT_NORM (1.0f / (1 << 23))  /* 23-bit mantissa → [0, 1) */
static inline float fast_log2(float x)
{
    union { float f; int32_t i; } vx;
    vx.f = (float)x;
    int32_t exp = (vx.i >> 23) & 0xFF;
    float m = (float)(vx.i & 0x7FFFFF) * FAST_LOG2_MANT_NORM;
    return (float)(exp - 127) + (float)(m * (FAST_LOG2_A - FAST_LOG2_B * m));
}

/* 64-band subband energy analysis using a 64-point complex FFT.
 * Leverages conjugate symmetry to extract two 64-point real-subsequence
 * DFTs from one complex transform, reducing FLOPs by ~50% compared to
 * a standard 128-point implementation. Phase info is discarded as the
 * SBR bitstream only transmits envelope magnitudes. */
/* noinline is deliberate. Both the HE-v1 energy path and the HE-v2 stereo path
 * call this, and letting LTO inline it into each gave the FFT two call contexts,
 * which it answered by cloning the entire transform (fft.part.0.constprop.0,
 * +1442 bytes) -- for a body far too large to earn anything back from inlining. */
#if defined(__GNUC__)
__attribute__((hot, noinline))
#endif
void SbrQmfAnalysisComplex(SBRInfo *sbr, const float * restrict ovl_pos, float * restrict xr_out, float * restrict xi_out, int kx, int k2)
{
    float xr[64], xi[64];
    const sbrfloat * restrict p0 = qmf_c;
    const sbrfloat * restrict p1 = qmf_c + 1;
    for (int m = 0; m < 64; m++) {
        int n0 = 2 * m;
        float a = p0[0]   * ovl_pos[639 - n0]
                    + p0[128] * ovl_pos[511 - n0]
                    + p0[256] * ovl_pos[383 - n0]
                    + p0[384] * ovl_pos[255 - n0]
                    + p0[512] * ovl_pos[127 - n0];
        float b = p1[0]   * ovl_pos[638 - n0]
                    + p1[128] * ovl_pos[510 - n0]
                    + p1[256] * ovl_pos[382 - n0]
                    + p1[384] * ovl_pos[254 - n0]
                    + p1[512] * ovl_pos[126 - n0];
        /* c[m] = (a + j*b) * exp(-j*pi*m/64) */
        xr[m] = a * sbr->twidCos[m] - b * sbr->twidSin[m];
        xi[m] = -(a * sbr->twidSin[m] + b * sbr->twidCos[m]);
        p0 += 2; p1 += 2;
    }
    fft(sbr->fftTables, xr, xi, 6);
    for (int k = kx; k < k2; k++) {
        int kr = 63 - k;
        /* Separate the two real-subsequence DFTs by conjugate symmetry. */
        float Ar = 0.5f * (xr[k] + xr[kr]);
        float Ai = 0.5f * (xi[kr] - xi[k]);
        float Br = -0.5f * (xi[k] + xi[kr]);
        float Bi = 0.5f * (xr[kr] - xr[k]);
        /* Sr = Ar + w_k_real * Br - w_k_imag * Bi
         * Si = Ai + w_k_real * Bi + w_k_imag * Br */
        float wr = sbr->oddCos[k];
        float wi = sbr->oddSin[k];
        xr_out[k] = Ar + wr * Br - wi * Bi;
        xi_out[k] = Ai + wr * Bi + wi * Br;
    }
}

#if defined(__GNUC__)
__attribute__((hot))
#endif
void SbrQmfAnalysis(SBRInfo *sbr, const float * restrict ovl_pos, float * restrict energy, int kx, int k2)
{
    float xr_out[64], xi_out[64];
    SbrQmfAnalysisComplex(sbr, ovl_pos, xr_out, xi_out, kx, k2);
    for (int k = kx; k < k2; k++)
        energy[k] = xr_out[k] * xr_out[k] + xi_out[k] * xi_out[k];
}


static void sbr_adopt_envelope_grid(const SBRInfo *sbr, const struct SignalAnalysis *sa, SbrFrameData *fd)
{
    fd->numEnvelopes = sa->numEnvelopes;
    fd->frameClass   = sa->frameClass;
    fd->bsPointer    = sa->bsPointer;
    for (int i = 0; i <= sa->numEnvelopes; i++) fd->tEnv[i] = sa->tEnv[i];
    fd->eff_amp_res = (fd->numEnvelopes == 1) ? 0 : sbr->bs_amp_res;
}

static void sbr_quantize_envelopes(const SBRInfo *sbr, int nch, int sampled,
                                   const struct SignalAnalysis *sa, SbrFrameData *fd)
{
    int n_env = fd->numEnvelopes;

    for (int ch = 0; ch < nch; ch++) {
        /* Read-only alias; the quantizer never writes back through it. */
        const float (* restrict bandHalfE)[SBR_QMF_BANDS_64] = sa->ch[ch].bandHalfE;
        int noise_level = SBR_NOISE_LEVEL_DEFAULT;
        fd->ch[ch].invfMode = 3;

        int dlav = fd->eff_amp_res ? SBR_ENV_DELTA_LIMIT_HIRES : SBR_ENV_DELTA_LIMIT_LORES;
        for (int e = 0; e < n_env; e++) {
            int prevLevel = -1;
            for (int b = 0; b < sbr->numBands; b++) {
                int k_lo = sbr->bandEdges[b], k_hi = sbr->bandEdges[b+1];
                /* Weight energy by the number of QMF slots per envelope to
                 * maintain normalized power levels across variable borders. */
                int e_slots = (n_env == 1) ? sampled : sa->envSampled[e];
                if (e_slots < 1) e_slots = 1;
                float E = 0;
                if (n_env == 1) {
                    for (int k = k_lo; k < k_hi; k++) E += bandHalfE[0][k] + bandHalfE[1][k];
                } else {
                    for (int k = k_lo; k < k_hi; k++) E += bandHalfE[e][k];
                }
                E /= (float)(e_slots * (k_hi - k_lo));
                float factor = fd->eff_amp_res ? 1.0f : 2.0f;
                int level = lrintf(factor * (fast_log2(E + SBR_LOG_ENERGY_FLOOR) - SBR_ENV_LEVEL_LOG2_OFFSET));
                int raw_level = clamp_int(level, 0, 127);
                if (prevLevel < 0) {
                    raw_level = clamp_int(raw_level, 0, fd->eff_amp_res ? 63 : 127);
                    fd->ch[ch].envData[e][b] = raw_level;
                    prevLevel = raw_level;
                } else {
                    int delta = clamp_int(raw_level - prevLevel, -dlav, dlav);
                    fd->ch[ch].envData[e][b] = delta;
                    prevLevel += delta;
                }
            }
        }
        int n_q = n_env > 1 ? 2 : 1;
        for (int ne = 0; ne < n_q; ne++) {
            int prevNoise = -1;
            for (int nb = 0; nb < sbr->numNoiseBands; nb++) {
                if (prevNoise < 0) {
                    fd->ch[ch].noiseData[ne][nb] = noise_level;
                    prevNoise = noise_level;
                } else {
                    int delta = clamp_int(noise_level - prevNoise, -15, 15);
                    fd->ch[ch].noiseData[ne][nb] = delta; prevNoise += delta;
                }
            }
        }
    }
}

/* PS parameter band -> QMF band [start, end). ISO/IEC 14496-3 Table 8.34 maps
 * each hybrid channel to one of 20 parameter bands (the map FFmpeg ships as
 * ff_k_to_i_20); the 10-band modes pair adjacent 20-band parameters, which
 * collapses that map to the ranges below. Note how uneven it is -- band 9 alone
 * covers QMF 23..63, i.e. everything above ~8.6 kHz at 48 kHz output.
 *
 * PS bands 0 and 1 are hybrid sub-bands *inside* QMF band 0. We run no hybrid
 * filterbank, so they cannot be told apart here; both take QMF band 0 and the
 * frequency-delta coder spends a single bit on the repeat. */
static const unsigned char ps_band_qmf[SBR_PS_BANDS][2] = {
    { 0,  1}, { 0,  1}, { 1,  2}, { 2,  3}, { 3,  5},
    { 5,  7}, { 7,  9}, { 9, 14}, {14, 23}, {23, 64}
};

/* IID levels the decoder dequantizes to, in dB, for index -7..+7 (ISO/IEC
 * 14496-3 Table 8.24, the "default" resolution selected by iid_mode 0). The
 * spacing is deliberately non-uniform -- 2 dB near centre, 7 dB at the edges --
 * so a uniform ladder would mis-pan every band it did not land exactly on.
 * Stored as the midpoints between neighbouring levels, which is all the
 * nearest-level search needs. */
static const float ps_iid_thresh_db[SBR_PS_IID_LEVELS - 1] = {
    -21.5f, -16.0f, -12.0f, -8.5f, -5.5f, -3.0f, -1.0f,
      1.0f,   3.0f,   5.5f,  8.5f, 12.0f, 16.0f, 21.5f
};

/* What each ICC index actually *achieves*, not the nominal value it stands for.
 *
 * The spec's dequantization points (Table 8.26) are
 *   { 1.0, 0.937, 0.84118, 0.60092, 0.36764, 0.0, -0.589, -1.0 }
 * but those describe a per-band target, and what survives to the decoder's
 * output is consistently more correlated than that -- the decorrelator is an
 * allpass network of finite length and its effect is diluted by the bands PS
 * groups together. Measured end to end through ffmpeg, forcing one index across
 * all bands and reading back broadband coherence:
 *
 *     index      0      1      2      3      4      5      6      7
 *     nominal  1.000  0.937  0.841  0.601  0.368  0.000 -0.589 -1.000
 *     achieved 0.973  0.930  0.863  0.687  0.502  0.173 -0.441 -0.970
 *
 * Quantizing a measured source coherence against the nominal column therefore
 * lands systematically too coherent -- the image collapses toward mono, by
 * +0.108 on average across the corpus, which was essentially the whole of our
 * stereo error. Choosing the index whose *achieved* value is nearest the source
 * removes that bias. What we transmit is unchanged and still means exactly what
 * the spec says; this only changes which index we pick. */
static const float ps_icc_quant[SBR_PS_ICC_LEVELS] = {
    0.973f, 0.930f, 0.863f, 0.687f, 0.502f, 0.173f, -0.441f, -0.970f
};

/* Estimate this frame's parametric stereo parameters into its delay-line slot,
 * update the downmix gain, and rewrite channel 0's band energies to describe the
 * mono signal the core will actually encode -- which is what the envelope
 * quantizer reads next.
 *
 * Everything comes out of the per-band energies SbrAnalyze already accumulated:
 * E_L, E_R and the cross term Re{L conj(R)}. No extra analysis pass. */
static void sbr_analyze_parametric_stereo(SBRInfo *sbr, struct SignalAnalysis *sa, SbrFrameData *fd)
{
    int n_env = sa->numEnvelopes;
    float totL = 0.0f, totR = 0.0f, totLR = 0.0f;

    for (int b = 0; b < SBR_PS_BANDS; b++) {
        float eL = 0.0f, eR = 0.0f, eLR = 0.0f, eLRi = 0.0f;

        for (int h = 0; h < n_env; h++) {
            for (int k = ps_band_qmf[b][0]; k < ps_band_qmf[b][1]; k++) {
                eL  += sa->ch[0].bandHalfE[h][k];
                eR  += sa->ch[1].bandHalfE[h][k];
                eLR += sa->bandCrossE[h][k];
                eLRi += sa->bandCrossIm[h][k];
            }
        }

        /* IID: pick the level nearest in dB. 10*log10(eL/eR) == 10/log2(10) *
         * log2(eL/eR), and fast_log2 is already here for the envelope coder. */
        float iid_db = 3.01029996f * fast_log2((eL + SBR_ENERGY_FLOOR) / (eR + SBR_ENERGY_FLOOR));
        int lvl = 0;
        while (lvl < SBR_PS_IID_LEVELS - 1 && iid_db >= ps_iid_thresh_db[lvl])
            lvl++;
        fd->iid[b] = lvl - (SBR_PS_IID_LEVELS / 2);

        /* ICC: normalized cross-correlation, nearest quantized value. Low
         * bands use the signed real part, where in- versus out-of-phase is
         * audible as such; above that, the magnitude, which cannot go negative.
         * See SBR_PS_ICC_SIGNED_BANDS. */
        float xnum = (b < SBR_PS_ICC_SIGNED_BANDS)
                     ? eLR : sqrtf(eLR * eLR + eLRi * eLRi);
        float icc = xnum / (sqrtf(eL * eR) + SBR_ENERGY_FLOOR);
        icc = clamp_float(icc, -1.0f, 1.0f);
        int best_icc = 0;
        float best_err = 2.0f;
        for (int i = 0; i <= SBR_PS_ICC_MAX_INDEX; i++) {
            float err = fabsf(icc - ps_icc_quant[i]);
            if (err < best_err) {
                best_err = err;
                best_icc = i;
            }
        }
        fd->icc[b] = best_icc;

        /* Band 0 duplicates band 1's QMF range, so summing every band would
         * double-count QMF band 0. */
        if (b != 0) {
            totL  += eL;
            totR  += eR;
            totLR += eLR;
        }
    }

    /* Signal ICC only when some band is actually decorrelated. Index 0 is
     * ICC = 1.0 (fully coherent), for which the decoder's default is identical
     * and the payload bits would be wasted. */
    fd->enable_icc = 0;
    for (int b = 0; b < SBR_PS_BANDS; b++) {
        if (fd->icc[b] > 0) {
            fd->enable_icc = 1;
            break;
        }
    }

    /* Energy-preserving downmix gain. The decoder's PS gains satisfy
     * c_l^2 + c_r^2 = 2, so it reconstructs 2*E_M and implicitly assumes the
     * mono it was handed carries E_M = (E_L + E_R)/2. A plain 0.5*(L+R) instead
     * carries (E_L + E_R + 2*E_LR)/4, which collapses toward silence as the
     * channels decorrelate. g restores the level.
     *
     * E_LR <= (E_L + E_R)/2 by Cauchy-Schwarz, so g >= 1 always; the ceiling
     * caps how much near-antiphase material may be boosted, and the one-pole
     * keeps the gain from pumping frame to frame. */
    float sum = totL + totR;
    float g = sqrtf(2.0f * sum / (sum + 2.0f * totLR + SBR_ENERGY_FLOOR));
    g = clamp_float(g, 1.0f, SBR_PS_MAX_DOWNMIX_GAIN);
    sbr->downmixGain += SBR_PS_GAIN_SMOOTH * (g - sbr->downmixGain);

    /* The envelope quantizer is about to read channel 0's band energies as "the
     * core's signal". Until now that was the *left* channel, which is only right
     * when L == R. Replace it with the true per-band energy of g * 0.5 * (L + R).
     *
     * Above the crossover the right answer is a different one, and better. The
     * decoder does not hear the core there at all: SBR regenerates those bands
     * and scales them to the level we transmit, and PS then splits that level
     * between L and R with c_l^2 + c_r^2 = 2 -- it hands back twice what it was
     * given. Transmitting (E_L + E_R)/2 therefore lands the reconstruction on
     * the band's true stereo energy no matter what the downmix lost there.
     *
     * That is per-band downmix compensation, which otherwise needs the downmix
     * moved into the QMF domain and a synthesis filterbank to get back out. For
     * the SBR range it costs one comparison, because the envelope *is* a
     * per-band gain we already transmit. Below the crossover the level is
     * whatever the core coded and no envelope can change it, so there the
     * downmix energy remains the honest description.
     *
     * Worth 0.02 of MOS at every rate measured, and slightly better coherence
     * with it -- which is the whole of what uncapping the ICC quantizer cost.
     * See SBR_PS_ICC_MAX_INDEX. */
    float gg = 0.25f * sbr->downmixGain * sbr->downmixGain;
    for (int h = 0; h < n_env; h++) {
        float * restrict eM = sa->ch[0].bandHalfE[h];
        const float * restrict eR2 = sa->ch[1].bandHalfE[h];
        const float * restrict eX = sa->bandCrossE[h];
        for (int k = 0; k < SBR_QMF_BANDS_64; k++)
            eM[k] = (k >= sbr->kx) ? 0.5f * (eM[k] + eR2[k])
                                   : gg * (eM[k] + eR2[k] + 2.0f * eX[k]);
    }
}

void SbrEncode(SBRInfo *sbr, float *timeDomain[MAX_CHANNELS], int numChannels, int numSamples, struct SignalAnalysis *sa, SbrFrameData *fd)
{
    int nch = clamp_int(numChannels, 1, SBR_MAX_CODED_CHANNELS);
    /* HE-AAC v2 analyses two input channels but codes one: the core sees a
     * downmix, so exactly one set of envelopes is quantized. */
    int coded_nch = SbrIsHEV2(sbr) ? 1 : nch;

    /* New frame: freeze the header-send decision now, before SbrWrite's write
     * pass (later, in the bitstream stage) mutates headerSent/frameCount. */
    sbr->sendHeaderThisFrame = (!sbr->headerSent || (sbr->frameCount % SBR_HEADER_PERIOD == 0));

    for (int ch = 0; ch < nch; ch++)
        memcpy(sbr->ch[ch].qmfOvl64, timeDomain[ch] + numSamples - SBR_QMF_OVL_LEN_64, SBR_QMF_OVL_LEN_64 * sizeof(float));

    sbr_adopt_envelope_grid(sbr, sa, fd);

    /* PS runs before the envelope quantizer: it derives the downmix gain, and
     * the envelopes must describe the gained downmix the core actually codes. */
    if (SbrIsHEV2(sbr) && nch == 2)
        sbr_analyze_parametric_stereo(sbr, sa, fd);

    sbr_quantize_envelopes(sbr, coded_nch, sa->sampled, sa, fd);
}

/* SBR bitstream writer. Emits the SBR fill element payload into the bitstream.
 * Replays the write sequence into a counting sink during rate control to
 * ensure accurate bit budget allocation. */

