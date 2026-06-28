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
    int row;
    if (singleRate) row = 6;
    else row = (sampleRate <= 16000) ? 0 : (sampleRate <= 22050) ? 1 : (sampleRate <= 24000) ? 2 : (sampleRate <= 32000) ? 3 : (sampleRate <= 64000) ? 4 : 5;
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
    sbr->singleRate = singleRate;
    unsigned long rate_per_ch = bitRate / channels;
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

    /* In single-rate mode, the QMF produces 64 bands over 2048 samples, but
     * we only have 1024 samples per frame. We use a 32-sample hop (producing
     * 32 slots) and consolidate the 64 QMF bands into 32 bands by summing
     * adjacent pairs. All bitstream indexing (kx, k2, frequency tables) must
     * therefore operate in this 32-band space. */
    if (singleRate) {
        sbr->kx >>= 1;
        sbr->k2 >>= 1;
        if (sbr->k2 <= sbr->kx) sbr->k2 = sbr->kx + 1;
    }

    build_freq_table(sbr);
    /* Twiddles for the 64-point FFT-based QMF (see qmf_analysis_64_slot_energy_fft). */
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
    return sbr;
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
void qmf_analysis_64_slot_energy_fft(SBRInfo *sbr, const faac_real * restrict ovl_pos, faac_real * restrict energy, int kx, int k2)
{
    faac_real xr[64], xi[64];
    const sbrfloat * restrict proto = qmf_c;
    for (int m = 0; m < 64; m++) {
        int n0 = 2 * m, n1 = 2 * m + 1;
        faac_real a = proto[n0]       * ovl_pos[639 - n0]
                    + proto[n0 + 128] * ovl_pos[511 - n0]
                    + proto[n0 + 256] * ovl_pos[383 - n0]
                    + proto[n0 + 384] * ovl_pos[255 - n0]
                    + proto[n0 + 512] * ovl_pos[127 - n0];
        faac_real b = proto[n1]       * ovl_pos[639 - n1]
                    + proto[n1 + 128] * ovl_pos[511 - n1]
                    + proto[n1 + 256] * ovl_pos[383 - n1]
                    + proto[n1 + 384] * ovl_pos[255 - n1]
                    + proto[n1 + 512] * ovl_pos[127 - n1];
        /* c[m] = (a + j*b) * exp(-j*pi*m/64) */
        xr[m] = a * sbr->twidCos[m] - b * sbr->twidSin[m];
        xi[m] = -(a * sbr->twidSin[m] + b * sbr->twidCos[m]);
    }
    fft(sbr->fftTables, xr, xi, 6);
    for (int k = kx; k < k2; k++) {
        int kr = 63 - k;
        /* Separate the two real-subsequence DFTs by conjugate symmetry. */
        faac_real Ar = (faac_real)0.5 * (xr[k] + xr[kr]);
        faac_real Ai = (faac_real)0.5 * (xi[kr] - xi[k]);
        faac_real Br = (faac_real)-0.5 * (xi[k] + xi[kr]);
        faac_real Bi = (faac_real)0.5 * (xr[kr] - xr[k]);
        faac_real Sr = Ar + sbr->oddCos[k] * Br - sbr->oddSin[k] * Bi;
        faac_real Si = Ai + sbr->oddCos[k] * Bi + sbr->oddSin[k] * Br;
        energy[k] = Sr * Sr + Si * Si;
    }
}


void SBRAnalysis(SBRInfo *sbr, faac_real *timeDomain[MAX_CHANNELS], int numChannels, int numSamples, struct SignalAnalysis *sa)
{
    int hop = sbr->singleRate ? 32 : 64;
    int num_slots = numSamples / hop, kx = sbr->kx, k2 = sbr->k2;
    int nch = clamp_int(numChannels, 1, 2);
    int half_slots = num_slots / 2 > 0 ? num_slots / 2 : 1;
    faac_real bandHalfE[2][2][SBR_QMF_BANDS_64];
    faac_real tratio = (faac_real)0.0;

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

            for (int slot = 0; slot < num_slots; slot++) {
#if FAAC_SBR_DECIMATION > 1
                if (slot % FAAC_SBR_DECIMATION == 0)
#endif
                {
                    faac_real slotEnergy[SBR_QMF_BANDS_64];
                    qmf_analysis_64_slot_energy_fft(sbr, workspace + slot * hop, slotEnergy, 0, 64);
                    int h = clamp_int(slot >= half_slots ? 1 : 0, 0, 1);
                    if (sbr->singleRate) {
                        for (int k = kx; k < k2; k++)
                            bandHalfE[ch][h][k] += slotEnergy[2 * k] + slotEnergy[2 * k + 1];
                    } else {
                        for (int k = kx; k < k2; k++)
                            bandHalfE[ch][h][k] += slotEnergy[k];
                    }
                }
            }
        }
        memcpy(sbr->ch[ch].qmfOvl64, timeDomain[ch] + numSamples - SBR_QMF_OVL_LEN_64, SBR_QMF_OVL_LEN_64 * sizeof(faac_real));
    }

    sbr->numEnvelopes = (tratio > SBR_TRANSIENT_THRESH_DEFAULT) ? 2 : 1;
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
                int e_slots = (n_env == 1) ? sampled : sampled / 2;
                if (e_slots < 1) e_slots = 1;
                faac_real E = 0;
                if (n_env == 1) {
                    for (int k = k_lo; k < k_hi; k++) E += bandHalfE[ch][0][k] + bandHalfE[ch][1][k];
                } else {
                    for (int k = k_lo; k < k_hi; k++) E += bandHalfE[ch][e][k];
                }
                E /= (faac_real)(e_slots * (k_hi - k_lo));
                faac_real factor = sbr->eff_amp_res ? (faac_real)1.0 : (faac_real)2.0;
                /* SBR_ENV_LEVEL_LOG2_OFFSET (6.0) normalizes 64-band QMF energy.
                 * Consolidating to 32 bands adds 3dB (+1.0 log2). */
                faac_real offset = sbr->singleRate ? (SBR_ENV_LEVEL_LOG2_OFFSET + 1.0f) : SBR_ENV_LEVEL_LOG2_OFFSET;
                int level = FAAC_LRINT(factor * (fast_log2(E + SBR_LOG_ENERGY_FLOOR) - offset));
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

static void write_sbr_grid(SBRInfo *sbr, BitStream *bs)
{
    /* FIXFIX: equal-spaced borders, one bs_freq_res for all envelopes.
     * Variable frame classes (VARFIX/FIXVAR/VARVAR) land in a later step. */
    PutBit(bs, SBR_FRAME_CLASS_FIXFIX, 2);
    PutBit(bs, sbr->numEnvelopes > 1 ? 1 : 0, 2);
    PutBit(bs, sbr->bs_freq_res, 1);
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

int SBRWriteBitstream(SBRInfo *sbr, BitStream *bs, int id_aac, int writeFlag, struct SignalAnalysis *sa)
{
    if (!sbr || !sbr->sbrPresent) return 0;
    int sendHeader = (!sbr->headerSent || (sbr->frameCount % SBR_HEADER_PERIOD == 0));

    /* Pass 1: measure the payload by replaying it into a counting sink. This is
     * the single source of truth for the fill_element byte count. */
    BitStream counter;
    memset(&counter, 0, sizeof(counter)); /* data==NULL => PutBit only counts */
    emit_sbr_payload(sbr, &counter, id_aac, sendHeader);
    int payloadBits = (int)counter.numBit;          /* ext_type + flag + header + data */
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

    /* Pass 2: emit the same payload for real (or just account for it). */
    if (writeFlag) emit_sbr_payload(sbr, bs, id_aac, sendHeader);
    totalBits += payloadBits;
    if (padBits > 0) WB(0, padBits);
#undef WB

    if (writeFlag) { sbr->headerSent = 1; sbr->frameCount++; }
    return totalBits;
}
