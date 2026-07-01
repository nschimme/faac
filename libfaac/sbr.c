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

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include "sbr.h"
#include "sbr_tables.h"
#include "bitstream.h"
#include "util.h"
#include "sbr_analysis.h"

static void put_huff(BitStream *bs, const SBRHuffEntry *table, int nsyms, int offset, int delta)
{
    int sym = clamp_int(delta + offset, 0, nsyms - 1);
    PutBit(bs, table[sym].code, table[sym].len);
}

/* kx and k2 implement ISO 14496-3:2009 §4.6.18.3.2 (Master Frequency Band Table).
 * temp (Table 4.84) and max_span (Table 4.85) are mandatory spec constants; do not tune. */
static int compute_kx(int sampleRate, int bs_start_freq, int singleRate)
{
    int temp = (sampleRate < 32000) ? 3000 : (sampleRate < 64000) ? 4000 : 5000;
    int start_min = ((temp << 7) + (sampleRate >> 1)) / sampleRate;
    int row = singleRate ? SBR_OFFSET_ROW_SINGLE_RATE
            : (sampleRate <= 16000) ? 0 : (sampleRate <= 22050) ? 1 : (sampleRate <= 24000) ? 2 : (sampleRate <= 32000) ? 3 : (sampleRate <= 64000) ? 4 : 5;
    return clamp_int(start_min + sbr_offset[row][bs_start_freq & 15], 1, 63);
}

static int cmp_int16(const void *a, const void *b) { return (int)(*(const short *)a) - (int)(*(const short *)b); }

static int compute_k2(int sampleRate, int kx, int bs_stop_freq)
{
    if (bs_stop_freq == 14 || bs_stop_freq == 15) return 64;
    int temp = (sampleRate < 32000) ? 3000 : (sampleRate < 64000) ? 4000 : 5000;
    int stop_min = ((temp << 8) + (sampleRate >> 1)) / sampleRate;
    int k2;
    if (bs_stop_freq < 14) {
        short stop_dk[13];
        faac_real prod = (faac_real)stop_min;
        int prev = stop_min;
        faac_real base = FAAC_POW((faac_real)64.0 / (faac_real)stop_min, (faac_real)(1.0 / 13.0));
        for (int i = 0; i < 12; i++) {
            prod *= base;
            int present = (int)FAAC_LRINT(prod);
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
    /* ISO 14496-3:2009 Table 4.85: max SBR span (48 bands ≤32 kHz, 35 ≤44.1 kHz, 32 above). */
    int max_span = (sampleRate <= 32000) ? 48 : (sampleRate <= 44100) ? 35 : 32;
    return clamp_int(k2, kx + 1, kx + max_span > 64 ? 64 : kx + max_span);
}

/* ISO 14496-3:2009 §4.6.18.3.2 master_frequency_table_fs():
 * uniform dk-spaced edges kx..k2, remainder distributed to first/last pair. */
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

SBRInfo *SBRInit(int channels, int sampleRate, unsigned long bitRate, int singleRate, FFT_Tables *fft_tables)
{
    SBRInfo *sbr = (SBRInfo *)AllocMemory(sizeof(SBRInfo));
    if (!sbr) return NULL;
    SetMemory(sbr, 0, sizeof(SBRInfo));
    sbr->sbrPresent = 1;
    sbr->numChannels = channels;
    sbr->sampleRate = sampleRate;

    /* Twiddles for the 64-point FFT-based QMF (see qmf_analysis_64_slot_energy_fft);
     * rate-independent, so they live in the one-time init, not SBRUpdate. */
    for (int m = 0; m < SBR_QMF_BANDS_64; m++) {
        sbr->twidCos[m] = (faac_real)cos(M_PI * m / 64.0);
        sbr->twidSin[m] = (faac_real)sin(M_PI * m / 64.0);
        sbr->oddCos[m] = (faac_real)cos(M_PI * (2 * m + 1) / 128.0);
        sbr->oddSin[m] = (faac_real)sin(M_PI * (2 * m + 1) / 128.0);
    }
    /* Borrow the encoder's shared core FFT tables (same fft() routine, same
     * logm=6 size as the short-block MDCT). The core owns init/terminate; the
     * logm=6 table is built lazily on first use, single-threaded per encoder. */
    sbr->fftTables = fft_tables;

    SBRUpdate(sbr, bitRate, singleRate);
    return sbr;
}

/* Band/bitrate configuration; split out of SBRInit so SetConfiguration can
 * re-resolve the sub-mode on an existing handle without reallocating. */
void SBRUpdate(SBRInfo *sbr, unsigned long bitRate, int singleRate)
{
    int sampleRate = sbr->sampleRate;
    sbr->singleRate = singleRate;
    unsigned long rate_per_ch = bitRate / sbr->numChannels;
    sbr->bs_amp_res = (rate_per_ch < SBR_AMP_RES_BITRATE_BPS) ? 0 : 1;
    /* Crossover at the top index (kx ~ 11.6 kHz, just under the Fs/2 core ceiling):
     * a ViSQOL sweep showed pushing it lower hurts at every bitrate (SBR's
     * parametric 8-12 kHz reconstruction is perceptually worse than the real,
     * if bit-starved, core there) -- including the low-rate range, where the old
     * bs_start_freq=12 cost ~0.4 MOS vs 15. */
    if (rate_per_ch <= SBR_COARSE_TABLE_BITRATE_BPS) {
        sbr->bs_start_freq = 15;
        sbr->bs_alter_scale = 1;
        sbr->dk = 2;
    } else {
        sbr->bs_start_freq = 15;
        sbr->bs_alter_scale = 0;
        sbr->dk = 1;
    }
    /* k2 lands ~3/4 of the way to Nyquist (18.4 kHz at 48 kHz input). */
    sbr->bs_stop_freq = 10;
    sbr->bs_freq_res = 1; /* HIGH resolution */
    sbr->bs_xover_band = 0; /* every master band is an SBR band; no low-res split */
    sbr->numEnvelopes = 1;
    sbr->eff_amp_res = (sbr->numEnvelopes == 1) ? 0 : sbr->bs_amp_res;
    sbr->kx = compute_kx(sampleRate, sbr->bs_start_freq, singleRate);
    sbr->k2 = compute_k2(sampleRate, sbr->kx, sbr->bs_stop_freq);

    /* Single-rate (rare): the QMF spans 64 bands over 2048 samples, but a
     * single-rate frame is only 1024 samples. AnalyzeSignal/SBRAnalysis run a
     * 32-sample hop (32 slots) and fold the 64 QMF bands into 32 by summing
     * adjacent pairs, so all bitstream indexing lives in the 32-band space. */
    if (singleRate) {
        sbr->kx >>= 1;
        sbr->k2 >>= 1;
        if (sbr->k2 <= sbr->kx) sbr->k2 = sbr->kx + 1;
    }

    build_freq_table(sbr);
}

void SBREnd(SBRInfo *sbr)
{
    if (!sbr) return;
    /* fftTables is borrowed from the encoder; the core terminates it. */
    FreeMemory(sbr);
}

/* Schlick '94 linear mantissa approx: log2(1+m) ≈ m*(A - B*m); max error ~0.086 bits.
 * Adequate: SBR envelope quantiser step is 1.5-3 dB (0.5-1 bit). */
#define FAST_LOG2_A         1.3424f
#define FAST_LOG2_B         0.3427f
#define FAST_LOG2_MANT_NORM (1.0f / (1 << 23))  /* 23-bit mantissa → [0, 1) */
static inline faac_real fast_log2(faac_real x)
{
    union { float f; int32_t i; } vx;
    vx.f = (float)x;
    int32_t exp = (vx.i >> 23) & 0xFF;
    float m = (float)(vx.i & 0x7FFFFF) * FAST_LOG2_MANT_NORM;
    return (faac_real)(exp - 127) + (faac_real)(m * (FAST_LOG2_A - FAST_LOG2_B * m));
}

/* 64-band analysis slot energy via a single 64-point complex FFT.
 *
 * Energy discards the per-band unit-modulus phase, so the modulation is just
 * the odd-frequency DFT S_k = sum_{n<128} u[n]*exp(-j*pi*(2k+1)*n/128) of the
 * REAL 128-tap window output u. Split u into even/odd a[m]=u[2m], b[m]=u[2m+1]
 * (m<64): S_k = A_k + w_k*B_k with w_k=exp(-j*pi*(2k+1)/128) and A,B the 64-pt
 * odd-DFTs of a,b. Pack c[m]=a[m]+j*b[m], pre-twiddle, and run ONE 64-point
 * FFT; A_k and B_k fall out of D_k and D_{63-k} via the conjugate symmetry of
 * the two real subsequences. ~2x fewer FFT flops than the 128-point form;
 * matches it to machine precision. Energy (magnitude squared) is sufficient:
 * the bitstream carries per-band magnitudes; the decoder reconstructs phase. */
/* Reached only through the cold HE dispatcher (doHEAACFrame). Under whole-program
 * LTO that coldness propagates here and the kernel is size-optimized (scalar,
 * un-vectorized); hot keeps this DSP loop vectorized while the dispatcher itself
 * stays out of the LC fast path. */
#if defined(__GNUC__)
__attribute__((hot))
#endif
void qmf_analysis_64_slot_energy_fft(SBRInfo *sbr, const faac_real * restrict ovl_pos, faac_real * restrict energy, int kx, int k2)
{
    faac_real xr[64], xi[64];
    const sbrfloat * restrict p0 = qmf_c;
    const sbrfloat * restrict p1 = qmf_c + 1;
    for (int m = 0; m < 64; m++) {
        int n0 = 2 * m;
        faac_real a = p0[0]   * ovl_pos[639 - n0]
                    + p0[128] * ovl_pos[511 - n0]
                    + p0[256] * ovl_pos[383 - n0]
                    + p0[384] * ovl_pos[255 - n0]
                    + p0[512] * ovl_pos[127 - n0];
        faac_real b = p1[0]   * ovl_pos[638 - n0]
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
        faac_real Ar = (faac_real)0.5 * (xr[k] + xr[kr]);
        faac_real Ai = (faac_real)0.5 * (xi[kr] - xi[k]);
        faac_real Br = (faac_real)-0.5 * (xi[k] + xi[kr]);
        faac_real Bi = (faac_real)0.5 * (xr[kr] - xr[k]);
        /* Sr = Ar + w_k_real * Br - w_k_imag * Bi
         * Si = Ai + w_k_real * Bi + w_k_imag * Br */
        faac_real wr = sbr->oddCos[k];
        faac_real wi = sbr->oddSin[k];
        faac_real Sr = Ar + wr * Br - wi * Bi;
        faac_real Si = Ai + wr * Bi + wi * Br;
        energy[k] = Sr * Sr + Si * Si;
    }
}


/* Single-rate (rare) fallback energy accumulation: the 64-band QMF is folded to
 * 32 bands by summing adjacent pairs. Kept out of line so the dual-rate path in
 * SBRAnalysis below compiles exactly as it did before single-rate existed. */
static void sbr_fallback_energy_single_rate(SBRInfo *sbr, const faac_real *workspace,
                                            int num_slots, int half_slots, int kx, int k2,
                                            faac_real bandHalfE[2][SBR_QMF_BANDS_64])
{
    for (int slot = 0; slot < num_slots; slot++) {
#if FAAC_SBR_DECIMATION > 1
        if (slot % FAAC_SBR_DECIMATION != 0) continue;
#endif
        faac_real slotEnergy[SBR_QMF_BANDS_64];
        qmf_analysis_64_slot_energy_fft(sbr, workspace + slot * SBR_QMF_BANDS_64, slotEnergy, 0, SBR_QMF_BANDS_64);
        int h = clamp_int(slot >= half_slots ? 1 : 0, 0, 1);
        for (int k = kx; k < k2; k++)
            bandHalfE[h][k] += slotEnergy[2 * k] + slotEnergy[2 * k + 1];
    }
}

/* Reached only through the cold HE dispatcher (doHEAACFrame). Under whole-program
 * LTO that coldness propagates here and the kernel is size-optimized (scalar,
 * un-vectorized); hot keeps this DSP loop vectorized while the dispatcher itself
 * stays out of the LC fast path. */
#if defined(__GNUC__)
__attribute__((hot))
#endif
void SBRAnalysis(SBRInfo *sbr, faac_real *timeDomain[MAX_CHANNELS], int numChannels, int numSamples, struct SignalAnalysis *sa)
{
    int num_slots = numSamples / SBR_QMF_BANDS_64, kx = sbr->kx, k2 = sbr->k2;
    int nch = clamp_int(numChannels, 1, 2);
    int half_slots = num_slots / 2 > 0 ? num_slots / 2 : 1;
    faac_real bandHalfE[2][2][SBR_QMF_BANDS_64];
    faac_real tratio = (faac_real)0.0;

    /* New frame: the cached fill-element payload (built by SBRWriteBitstream) is now
     * stale. Invalidate it so the next write rebuilds from this frame's envelopes. */
    sbr->payloadValid = 0;

    memset(bandHalfE, 0, sizeof(bandHalfE));
    for (int ch = 0; ch < nch; ch++) {
        /* Phase 1: Use shared transient strength and accumulated energies. */
        if (sa && sa->valid) {
            faac_real ratio = sa->ch[ch].transientStrength;
            if (ratio > tratio) tratio = ratio;
            memcpy(bandHalfE[ch][0], sa->ch[ch].bandHalfE[0], SBR_QMF_BANDS_64 * sizeof(faac_real));
            memcpy(bandHalfE[ch][1], sa->ch[ch].bandHalfE[1], SBR_QMF_BANDS_64 * sizeof(faac_real));
        } else {
            /* Fallback (LC path or missing analysis). */
            faac_real workspace[SBR_QMF_OVL_LEN_64 + 2 * FRAME_LEN];
            memcpy(workspace, sbr->ch[ch].qmfOvl64, SBR_QMF_OVL_LEN_64 * sizeof(faac_real));
            memcpy(workspace + SBR_QMF_OVL_LEN_64, timeDomain[ch], numSamples * sizeof(faac_real));

            if (sbr->singleRate) {
                sbr_fallback_energy_single_rate(sbr, workspace, num_slots, half_slots, kx, k2, bandHalfE[ch]);
            } else
            for (int slot = 0; slot < num_slots; slot++) {
#if FAAC_SBR_DECIMATION > 1
                if (slot % FAAC_SBR_DECIMATION == 0)
#endif
                {
                    faac_real slotEnergy[SBR_QMF_BANDS_64];
                    qmf_analysis_64_slot_energy_fft(sbr, workspace + slot * SBR_QMF_BANDS_64, slotEnergy, kx, k2);
                    int h = clamp_int(slot >= half_slots ? 1 : 0, 0, 1);
                    for (int k = kx; k < k2; k++) bandHalfE[ch][h][k] += slotEnergy[k];
                }
            }
        }
        memcpy(sbr->ch[ch].qmfOvl64, timeDomain[ch] + numSamples - SBR_QMF_OVL_LEN_64, SBR_QMF_OVL_LEN_64 * sizeof(faac_real));
    }

    /* Adopt the frame grid chosen by AnalyzeSignal (which also binned the
     * envelope energies over these borders). The fallback path has no analysis,
     * so it stays single-envelope FIXFIX. */
    if (sa && sa->valid) {
        sbr->numEnvelopes = sa->numEnvelopes;
        sbr->frameClass   = sa->frameClass;
        sbr->bsPointer    = sa->bsPointer;
        for (int i = 0; i <= sa->numEnvelopes; i++) sbr->tEnv[i] = sa->tEnv[i];
    } else {
        sbr->numEnvelopes = (tratio > SBR_TRANSIENT_THRESH_DEFAULT) ? 2 : 1;
        sbr->frameClass   = SBR_FRAME_CLASS_FIXFIX;
    }
    sbr->eff_amp_res = (sbr->numEnvelopes == 1) ? 0 : sbr->bs_amp_res;
    int n_env = sbr->numEnvelopes;
    int sampled = (sa && sa->valid) ? sa->sampled : (num_slots - 1) / FAAC_SBR_DECIMATION + 1;

    for (int ch = 0; ch < nch; ch++) {
        int noise_level = SBR_NOISE_LEVEL_DEFAULT;
        int invf_mode = 3;

        /* Phase 5: Tonality-driven noise floor / invf mode.
         * Note: Higher noiseData value = LOWER injected noise.
         * Tonality measure is E_orig / E_transposed.
         * High tonality (tonality -> 1.0) = original matches transposed = tonal = LESS noise needed = HIGHER noise_level.
         * Low tonality (tonality -> 0.0) = original much lower than transposed = noisy = MORE noise needed = LOWER noise_level. */
        if (sa && sa->valid) {
            faac_real avg_tonality = (faac_real)0.0;
            for (int k = kx; k < k2; k++) avg_tonality += sa->ch[ch].bandTonality[k];
            avg_tonality /= (faac_real)(k2 - kx);

            /* Scale noise_level from 4 (safe floor) to 12 (min noise).
             * Higher value = LOWER injected noise.
             * Low tonality content (noisy) needs more noise (lower noise_level). */
            noise_level = 4 + (int)FAAC_LRINT(8.0 * avg_tonality);
            noise_level = clamp_int(noise_level, 4, 12);

            /* invfMode: 0=OFF, 1=LOW, 2=MID, 3=HIGH whitening.
             * Tonal content (high tonality) wants OFF; noisy (low tonality) wants HIGH. */
            if (avg_tonality > 0.8) invf_mode = 0;
            else if (avg_tonality > 0.5) invf_mode = 1;
            else if (avg_tonality > 0.2) invf_mode = 2;
            else invf_mode = 3;
        }
        sbr->ch[ch].invfMode = invf_mode;

        int dlav = sbr->eff_amp_res ? 31 : 60;
        for (int e = 0; e < n_env; e++) {
            int prevLevel = -1;
            for (int b = 0; b < sbr->numBands; b++) {
                int k_lo = sbr->bandEdges[b], k_hi = sbr->bandEdges[b+1];
                /* Slots per envelope: with a variable border the two envelopes
                 * are unequal, so use the analysis bin counts when available. */
                int e_slots = (n_env == 1) ? sampled
                            : (sa && sa->valid) ? sa->envSampled[e]
                            : sampled / 2;
                if (e_slots < 1) e_slots = 1;
                faac_real E = 0;
                if (n_env == 1) {
                    for (int k = k_lo; k < k_hi; k++) E += bandHalfE[ch][0][k] + bandHalfE[ch][1][k];
                } else {
                    for (int k = k_lo; k < k_hi; k++) E += bandHalfE[ch][e][k];
                }
                E /= (faac_real)(e_slots * (k_hi - k_lo));
                faac_real factor = sbr->eff_amp_res ? (faac_real)1.0 : (faac_real)2.0;
                int level = FAAC_LRINT(factor * (fast_log2(E + SBR_LOG_ENERGY_FLOOR) - SBR_ENV_LEVEL_LOG2_OFFSET));
                int raw_level = clamp_int(level, 0, 127);
                if (prevLevel < 0) {
                    raw_level = clamp_int(raw_level, 0, sbr->eff_amp_res ? 63 : 127);
                    sbr->ch[ch].envData[e][b] = raw_level;
                    prevLevel = raw_level;
                } else {
                    int delta = clamp_int(raw_level - prevLevel, -dlav, dlav);
                    sbr->ch[ch].envData[e][b] = delta;
                    prevLevel += delta;
                }
            }
        }
        int n_q = n_env > 1 ? 2 : 1;
        for (int ne = 0; ne < n_q; ne++) {
            int prevNoise = -1;
            for (int nb = 0; nb < sbr->numNoiseBands; nb++) {
                if (prevNoise < 0) {
                    sbr->ch[ch].noiseData[ne][nb] = noise_level;
                    prevNoise = noise_level;
                } else {
                    int delta = clamp_int(noise_level - prevNoise, -15, 15);
                    sbr->ch[ch].noiseData[ne][nb] = delta; prevNoise += delta;
                }
            }
        }
    }
}

/* The SBR writer is single-pass: every helper unconditionally emits into the
 * BitStream it is given. Bit counting is done by replaying the exact same emit
 * sequence into a counting sink (a BitStream with data==NULL, see PutBit), so
 * the measured length and the written length can never diverge. This is what
 * lets the data-dependent variable grid (VARFIX) be counted safely. */

static void write_sbr_header(SBRInfo *sbr, BitStream *bs)
{
    /* ISO 14496-3:2009 §4.6.18.5 sbr_header() (21 bits) */
    PutBit(bs, sbr->bs_amp_res,     1); /* bs_amp_res: 0=1.5dB, 1=3dB */
    PutBit(bs, sbr->bs_start_freq,  4); /* bs_start_freq: crossover index */
    PutBit(bs, sbr->bs_stop_freq,   4); /* bs_stop_freq: high-band ceil */
    PutBit(bs, sbr->bs_xover_band,  3); /* bs_xover_band: low-res split (0=none) */
    PutBit(bs, 0,                   2); /* bs_reserved */
    PutBit(bs, 1,                   1); /* bs_header_extra_1 = 1 (alter_scale present) */
    PutBit(bs, 0,                   1); /* bs_header_extra_2 = 0 (limiter fields absent) */
    /* bs_header_extra_1 fields: */
    PutBit(bs, 0,                   2); /* bs_freq_scale = 0 (linear master table) */
    PutBit(bs, sbr->bs_alter_scale, 1); /* bs_alter_scale: 1=coarser at low bitrate */
    PutBit(bs, 0,                   2); /* bs_noise_bands = 0 (→ 1 noise band) */
}

/* bs_pointer width = ceil(log2(bs_num_env + 1)), indexed by bs_num_env.
 * Mirrors FFmpeg/FAAD2 ceil_log2[] (max num_env here is SBR_MAX_ENVELOPES=2). */
static const int sbr_ceil_log2[] = { 0, 1, 2, 2, 3, 3 };

static void write_sbr_grid(SBRInfo *sbr, BitStream *bs)
{
    if (sbr->frameClass == SBR_FRAME_CLASS_VARFIX) {
        /* VARFIX: variable leading borders, fixed trailing border. Mirrors the
         * inverse of FFmpeg read_sbr_grid()'s VARFIX case: t_env[0]=bs_var_bord_0,
         * each lead border adds 2*bs_rel+2, the trailing border is numTimeSlots
         * (not transmitted), then bs_pointer and per-envelope bs_freq_res. */
        int num_env = sbr->numEnvelopes;
        PutBit(bs, SBR_FRAME_CLASS_VARFIX, 2);
        PutBit(bs, sbr->tEnv[0], 2);                 /* bs_var_bord_0 */
        PutBit(bs, num_env - 1, 2);                  /* bs_num_rel_0   */
        for (int i = 0; i < num_env - 1; i++)
            PutBit(bs, (sbr->tEnv[i + 1] - sbr->tEnv[i] - 2) / 2, 2); /* bs_rel_bord */
        PutBit(bs, sbr->bsPointer, sbr_ceil_log2[num_env]);
        for (int i = 0; i < num_env; i++)            /* bs_freq_res[1..num_env] */
            PutBit(bs, sbr->bs_freq_res, 1);
    } else {
        /* FIXFIX: equal-spaced borders, one bs_freq_res for all envelopes. */
        PutBit(bs, SBR_FRAME_CLASS_FIXFIX, 2);
        PutBit(bs, sbr->numEnvelopes > 1 ? 1 : 0, 2);
        PutBit(bs, sbr->bs_freq_res, 1);
    }
}

static void write_sbr_dtdf(SBRInfo *sbr, BitStream *bs)
{
    int n_q = sbr->numEnvelopes > 1 ? 2 : 1;
    int bits = sbr->numEnvelopes + n_q;
    for (int i = 0; i < bits; i++) PutBit(bs, 0, 1);
}

static void write_sbr_invf(SBRInfo *sbr, BitStream *bs, int ch)
{
    for (int nb = 0; nb < sbr->numNoiseBands; nb++)
        PutBit(bs, sbr->ch[ch].invfMode, 2);
}

static void write_sbr_envelope(SBRInfo *sbr, BitStream *bs, int ch)
{
    const SBRHuffEntry *table = sbr->eff_amp_res ? f_huff_env_3_0dB : f_huff_env_1_5dB;
    int nsyms = sbr->eff_amp_res ? F_HUFF_ENV_3_0DB_NSYMS : F_HUFF_ENV_1_5DB_NSYMS;
    int offset = sbr->eff_amp_res ? F_HUFF_ENV_3_0DB_OFFSET : F_HUFF_ENV_1_5DB_OFFSET;

    for (int e = 0; e < sbr->numEnvelopes; e++) {
        for (int b = 0; b < sbr->numBands; b++) {
            int val = sbr->ch[ch].envData[e][b];
            if (b == 0) {
                int first_bits = sbr->eff_amp_res ? 6 : 7;
                PutBit(bs, clamp_int(val, 0, (1 << first_bits) - 1), first_bits);
            } else {
                put_huff(bs, table, nsyms, offset, val);
            }
        }
    }
}

static void write_sbr_noise(SBRInfo *sbr, BitStream *bs, int ch)
{
    int n_q = sbr->numEnvelopes > 1 ? 2 : 1;
    for (int ne = 0; ne < n_q; ne++) {
        for (int nb = 0; nb < sbr->numNoiseBands; nb++) {
            int val = sbr->ch[ch].noiseData[ne][nb];
            if (nb == 0) PutBit(bs, clamp_int(val, 0, 30), 5);
            else put_huff(bs, f_huff_env_3_0dB, F_HUFF_ENV_3_0DB_NSYMS, F_HUFF_ENV_3_0DB_OFFSET, val);
        }
    }
}

static void write_sbr_data(SBRInfo *sbr, BitStream *bs, int id_aac)
{
    if (id_aac == ID_CPE) {
        PutBit(bs, 0, 1); PutBit(bs, 0, 1);     /* bs_coupling=0, reserved */
        write_sbr_grid(sbr, bs);
        write_sbr_grid(sbr, bs);
        write_sbr_dtdf(sbr, bs);
        write_sbr_dtdf(sbr, bs);
        write_sbr_invf(sbr, bs, 0);
        write_sbr_invf(sbr, bs, 1);
        write_sbr_envelope(sbr, bs, 0);
        write_sbr_envelope(sbr, bs, 1);
        write_sbr_noise(sbr, bs, 0);
        write_sbr_noise(sbr, bs, 1);
        PutBit(bs, 0, 1); PutBit(bs, 0, 1); PutBit(bs, 0, 1); /* add_harmonic / extended data flags */
    } else {
        PutBit(bs, 0, 1);                        /* reserved */
        write_sbr_grid(sbr, bs);
        write_sbr_dtdf(sbr, bs);
        write_sbr_invf(sbr, bs, 0);
        write_sbr_envelope(sbr, bs, 0);
        write_sbr_noise(sbr, bs, 0);
        PutBit(bs, 0, 1); PutBit(bs, 0, 1);      /* add_harmonic / extended data flags */
    }
}

/* Emit the full extension_payload body for EXT_SBR_DATA: the 4-bit extension
 * type, the 1-bit header flag, the optional header, and the channel data. */
static void emit_sbr_payload(SBRInfo *sbr, BitStream *bs, int id_aac, int sendHeader)
{
    PutBit(bs, SBR_EXT_TYPE_SBR, 4);
    PutBit(bs, sendHeader, 1);
    if (sendHeader) write_sbr_header(sbr, bs);
    write_sbr_data(sbr, bs, id_aac);
}

/* Replay a byte-aligned, MSB-first payload (as packed into payloadBuf by PutBit)
 * into the real bitstream. This is the "dumb write" — no grid/Huffman logic, so it
 * reproduces the cached emit bit-for-bit. */
static void blit_payload(BitStream *bs, const unsigned char *buf, int nbits)
{
    int fullBytes = nbits >> 3, rem = nbits & 7;
    for (int i = 0; i < fullBytes; i++) PutBit(bs, buf[i], 8);
    if (rem) PutBit(bs, buf[fullBytes] >> (8 - rem), rem);
}

int SBRWriteBitstream(SBRInfo *sbr, BitStream *bs, int id_aac, int writeFlag, struct SignalAnalysis *sa)
{
    if (!sbr || !sbr->sbrPresent) return 0;

    /* Build the payload once per frame, then reuse it across the three call sites
     * (CountBitstream, the real WriteBitstream, and rate control). The length and
     * bytes are frozen once SBRAnalysis finishes — they depend only on sbr state
     * plus the header-period flag — so re-emitting the grid/Huffman logic on every
     * call is pure waste. SBRAnalysis clears payloadValid once per frame; the first
     * write after it rebuilds. Built before frameCount++ so cachedSendHeader is the
     * value all of this frame's calls must use. payloadBuf is sized to the array-
     * bound worst case (~690 bytes for a maxed CPE); the assert below still guards
     * the separate fill_element esc_count limit. */
    if (!sbr->payloadValid) {
        sbr->cachedSendHeader = (!sbr->headerSent || (sbr->frameCount % SBR_HEADER_PERIOD == 0));
        BitStream cb;
        memset(&cb, 0, sizeof(cb));
        cb.data = sbr->payloadBuf;
        cb.size = sizeof(sbr->payloadBuf);
        emit_sbr_payload(sbr, &cb, id_aac, sbr->cachedSendHeader);
        sbr->payloadBits = (int)cb.numBit;          /* ext_type + flag + header + data */
        sbr->payloadValid = 1;
    }

    int payloadBits = sbr->payloadBits;
    int fillBytes = (payloadBits + 7) / 8;
    int padBits = fillBytes * 8 - payloadBits;

    /* The fill_element count escapes through an 8-bit field, so a single
     * extension_payload tops out at 15 + 255 - 1 = 269 bytes. A larger SBR
     * payload would silently truncate esc_count and corrupt the boundary. */
    assert(fillBytes <= 14 + 255);

    int totalBits = 0;
#define WB(v,n) do { if (writeFlag) PutBit(bs,(v),(n)); totalBits += (n); } while(0)
    /* fill_element(): id, then 4-bit count with optional 8-bit escape. The
     * decoder reconstructs cnt = 15 + esc_count - 1, hence esc_count = N - 14. */
    WB(ID_FIL, 3);
    if (fillBytes < 15) WB(fillBytes, 4);
    else { WB(15, 4); WB(fillBytes - 14, 8); }

    /* Blit the cached payload for real (counting-only callers just account for it). */
    if (writeFlag) blit_payload(bs, sbr->payloadBuf, payloadBits);
    totalBits += payloadBits;
    if (padBits > 0) WB(0, padBits);
#undef WB

    if (writeFlag) { sbr->headerSent = 1; sbr->frameCount++; }
    return totalBits;
}
