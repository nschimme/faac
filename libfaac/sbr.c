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

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "sbr.h"
#include "sbr_tables.h"
#include "bitstream.h"
#include "util.h"

static int clamp_int(int x, int lo, int hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static int put_huff(BitStream *bs, const SBRHuffEntry *table, int nsyms, int offset, int delta, int writeFlag)
{
    int sym = delta + offset;
    if (sym < 0) sym = 0;
    if (sym >= nsyms) sym = nsyms - 1;
    if (writeFlag) PutBit(bs, table[sym].code, table[sym].len);
    return table[sym].len;
}

/* kx and k2 implement ISO 14496-3:2009 §4.6.18.3.2 (Master Frequency Band Table).
 * temp (Table 4.84) and max_span (Table 4.85) are mandatory spec constants; do not tune. */
static int compute_kx(int sampleRate, int bs_start_freq)
{
    int temp = (sampleRate < 32000) ? 3000 : (sampleRate < 64000) ? 4000 : 5000;
    int start_min = ((temp << 7) + (sampleRate >> 1)) / sampleRate;
    int row = (sampleRate <= 16000) ? 0 : (sampleRate <= 22050) ? 1 : (sampleRate <= 24000) ? 2 : (sampleRate <= 32000) ? 3 : (sampleRate <= 64000) ? 4 : 5;
    int kx = start_min + sbr_offset[row][bs_start_freq & 15];
    if (kx < 1) kx = 1;
    if (kx > 63) kx = 63;
    return kx;
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
    if (k2 > 64) k2 = 64;
    if (k2 <= kx) k2 = kx + 1;
    /* ISO 14496-3:2009 Table 4.85: max SBR span (48 bands ≤32 kHz, 35 ≤44.1 kHz, 32 above). */
    int max_span = (sampleRate <= 32000) ? 48 : (sampleRate <= 44100) ? 35 : 32;
    if (k2 - kx > max_span) k2 = kx + max_span;
    return k2;
}

/* ISO 14496-3:2009 §4.6.18.3.2 master_frequency_table_fs():
 * uniform dk-spaced edges kx..k2, remainder distributed to first/last pair. */
static int build_freq_table(SBRInfo *sbr)
{
    int kx = sbr->kx, k2 = sbr->k2, dk = sbr->dk;
    int n_master = ((k2 - kx + (dk & 2)) >> dk) << 1;
    if (n_master < 1) n_master = 1;
    if (n_master > SBR_MAX_BANDS) n_master = SBR_MAX_BANDS;
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

SBRInfo *SBRInit(int channels, int sampleRate, unsigned long bitRate, FFT_Tables *fft_tables)
{
    SBRInfo *sbr = (SBRInfo *)AllocMemory(sizeof(SBRInfo));
    if (!sbr) return NULL;
    SetMemory(sbr, 0, sizeof(SBRInfo));
    sbr->sbrPresent = 1;
    sbr->numChannels = channels;
    sbr->sampleRate = sampleRate;
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
    sbr->transientThresh = SBR_TRANSIENT_THRESH_DEFAULT;
    sbr->eff_amp_res = (sbr->numEnvelopes == 1) ? 0 : sbr->bs_amp_res;
    sbr->kx = compute_kx(sampleRate, sbr->bs_start_freq);
    sbr->k2 = compute_k2(sampleRate, sbr->kx, sbr->bs_stop_freq);
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
static void qmf_analysis_64_slot_energy_fft(SBRInfo *sbr, const faac_real * restrict ovl_pos, faac_real * restrict energy, int kx, int k2)
{
    faac_real xr[64], xi[64];
    const faac_real * restrict proto = qmf_c;
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


void SBRAnalysis(SBRInfo *sbr, faac_real *timeDomain[MAX_CHANNELS], int numChannels, int numSamples)
{
    int num_slots = numSamples / SBR_QMF_BANDS_64, kx = sbr->kx, k2 = sbr->k2;
    int nch = clamp_int(numChannels, 1, 2);
    int half_slots = num_slots / 2 > 0 ? num_slots / 2 : 1;
    faac_real bandHalfE[2][2][SBR_QMF_BANDS_64];
    faac_real slotEnergy[SBR_QMF_BANDS_64];
    faac_real tratio = (faac_real)0.0;
    faac_real workspace[SBR_QMF_OVL_LEN_64 + 2 * FRAME_LEN];

    int sampled = (num_slots - 1) / FAAC_SBR_DECIMATION + 1;
    memset(bandHalfE, 0, sizeof(bandHalfE));
    for (int ch = 0; ch < nch; ch++) {
        faac_real smax = (faac_real)0.0, ssum = (faac_real)0.0;
        memcpy(workspace, sbr->ch[ch].qmfOvl64, SBR_QMF_OVL_LEN_64 * sizeof(faac_real));
        memcpy(workspace + SBR_QMF_OVL_LEN_64, timeDomain[ch], numSamples * sizeof(faac_real));

        for (int slot = 0; slot < num_slots; slot++) {
            faac_real *ovl_pos = workspace + slot * SBR_QMF_BANDS_64;
            const faac_real * restrict p_in = timeDomain[ch] + slot * SBR_QMF_BANDS_64;

            faac_real stot = (faac_real)0.0;
            for (int n = 0; n < SBR_QMF_BANDS_64; n++) {
                faac_real val = *p_in++;
                stot += val * val;
            }
            if (stot > smax) smax = stot;
            ssum += stot;

#if FAAC_SBR_DECIMATION > 1
            if (slot % FAAC_SBR_DECIMATION == 0)
#endif
            {
                qmf_analysis_64_slot_energy_fft(sbr, ovl_pos, slotEnergy, kx, k2);
                int h = clamp_int(slot / half_slots, 0, 1);
                for (int k = kx; k < k2; k++) bandHalfE[ch][h][k] += slotEnergy[k];
            }
        }
        /* Peak-to-mean slot power ratio: >transientThresh (~12 dB) → 2-envelope frame. */
        faac_real ratio = smax * (faac_real)sampled / (ssum + SBR_ENERGY_FLOOR);
        if (ratio > tratio) tratio = ratio;
        memcpy(sbr->ch[ch].qmfOvl64, workspace + numSamples, SBR_QMF_OVL_LEN_64 * sizeof(faac_real));
    }

    sbr->numEnvelopes = (tratio > sbr->transientThresh) ? 2 : 1;
    sbr->eff_amp_res = (sbr->numEnvelopes == 1) ? 0 : sbr->bs_amp_res;
    int n_env = sbr->numEnvelopes;

    for (int ch = 0; ch < nch; ch++) {
        int noise_level = 0; /* min injection: non-zero noise is destructive if miscalibrated (TODO) */
        sbr->ch[ch].invfMode = 3; /* HIVAR: strongest pre-whitening; conservative until tonality adapts (TODO) */
        /* Huffman delta range: ISO Table 4.100 extents (3dB res: ±31, 1.5dB res: ±60). */
        int dlav = sbr->eff_amp_res ? 31 : 60;
        for (int e = 0; e < n_env; e++) {
            int prevLevel = -1;
            for (int b = 0; b < sbr->numBands; b++) {
                int k_lo = sbr->bandEdges[b], k_hi = sbr->bandEdges[b+1] > k_lo ? sbr->bandEdges[b+1] : k_lo + 1;
                if (k_hi > SBR_QMF_BANDS_64) k_hi = SBR_QMF_BANDS_64;
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
        int prevNoise = -1;
        for (int nb = 0; nb < sbr->numNoiseBands; nb++) {
            if (prevNoise < 0) {
                sbr->ch[ch].noiseData[nb] = noise_level;
                prevNoise = noise_level;
            } else {
                int delta = clamp_int(noise_level - prevNoise, -15, 15);
                sbr->ch[ch].noiseData[nb] = delta; prevNoise += delta;
            }
        }
    }
}

static int write_sbr_header(SBRInfo *sbr, BitStream *bs, int wf)
{
    /* ISO 14496-3:2009 §4.6.18.5 sbr_header() */
    if (wf) {
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
    return 1+4+4+3+2+1+1+2+1+2; /* = 21; keep in sync with PutBit sequence */
}

static int write_sbr_grid(SBRInfo *sbr, BitStream *bs, int wf)
{
    if (wf) {
        PutBit(bs, SBR_FRAME_CLASS_FIXFIX, 2);
        PutBit(bs, sbr->numEnvelopes > 1 ? 1 : 0, 2);
        PutBit(bs, sbr->bs_freq_res, 1);
    }
    return 5;
}

static int write_sbr_dtdf(SBRInfo *sbr, BitStream *bs, int wf)
{
    int n_q = sbr->numEnvelopes > 1 ? 2 : 1;
    int bits = sbr->numEnvelopes + n_q;
    if (wf) for (int i = 0; i < bits; i++) PutBit(bs, 0, 1);
    return bits;
}
static int write_sbr_invf(SBRInfo *sbr, BitStream *bs, int ch, int wf)
{
    for (int nb = 0; nb < sbr->numNoiseBands; nb++)
        if (wf) PutBit(bs, sbr->ch[ch].invfMode, 2);
    return 2 * sbr->numNoiseBands;
}

static int write_sbr_envelope(SBRInfo *sbr, BitStream *bs, int ch, int wf)
{
    int bits = 0;
    const SBRHuffEntry *table = sbr->eff_amp_res ? f_huff_env_3_0dB : f_huff_env_1_5dB;
    int nsyms = sbr->eff_amp_res ? F_HUFF_ENV_3_0DB_NSYMS : F_HUFF_ENV_1_5DB_NSYMS;
    int offset = sbr->eff_amp_res ? F_HUFF_ENV_3_0DB_OFFSET : F_HUFF_ENV_1_5DB_OFFSET;

    for (int e = 0; e < sbr->numEnvelopes; e++) {
        for (int b = 0; b < sbr->numBands; b++) {
            int val = sbr->ch[ch].envData[e][b];
            if (b == 0) {
                int first_bits = sbr->eff_amp_res ? 6 : 7;
                if (wf) PutBit(bs, clamp_int(val, 0, (1 << first_bits) - 1), first_bits);
                bits += first_bits;
            } else {
                bits += put_huff(bs, table, nsyms, offset, val, wf);
            }
        }
    }
    return bits;
}

static int write_sbr_noise(SBRInfo *sbr, BitStream *bs, int ch, int wf)
{
    int bits = 0;
    int n_q = sbr->numEnvelopes > 1 ? 2 : 1;
    for (int ne = 0; ne < n_q; ne++) {
        for (int nb = 0; nb < sbr->numNoiseBands; nb++) {
            int val = sbr->ch[ch].noiseData[nb];
            if (nb == 0) { if (wf) PutBit(bs, clamp_int(val, 0, 30), 5); bits += 5; }
            else bits += put_huff(bs, f_huff_env_3_0dB, F_HUFF_ENV_3_0DB_NSYMS, F_HUFF_ENV_3_0DB_OFFSET, val, wf);
        }
    }
    return bits;
}

static int write_sbr_data(SBRInfo *sbr, BitStream *bs, int id_aac, int wf)
{
    int bits = 0;
    if (id_aac == ID_CPE) {
        if (wf) { PutBit(bs, 0, 1); PutBit(bs, 0, 1); }
        bits += 2;
        bits += write_sbr_grid(sbr, bs, wf);
        bits += write_sbr_grid(sbr, bs, wf);
        bits += write_sbr_dtdf(sbr, bs, wf);
        bits += write_sbr_dtdf(sbr, bs, wf);
        bits += write_sbr_invf(sbr, bs, 0, wf);
        bits += write_sbr_invf(sbr, bs, 1, wf);
        bits += write_sbr_envelope(sbr, bs, 0, wf);
        bits += write_sbr_envelope(sbr, bs, 1, wf);
        bits += write_sbr_noise(sbr, bs, 0, wf);
        bits += write_sbr_noise(sbr, bs, 1, wf);
        if (wf) { PutBit(bs, 0, 1); PutBit(bs, 0, 1); PutBit(bs, 0, 1); }
        bits += 3;
    } else {
        if (wf) PutBit(bs, 0, 1);
        bits += 1;
        bits += write_sbr_grid(sbr, bs, wf);
        bits += write_sbr_dtdf(sbr, bs, wf);
        bits += write_sbr_invf(sbr, bs, 0, wf);
        bits += write_sbr_envelope(sbr, bs, 0, wf);
        bits += write_sbr_noise(sbr, bs, 0, wf);
        if (wf) { PutBit(bs, 0, 1); PutBit(bs, 0, 1); }
        bits += 2;
    }
    return bits;
}

int SBRWriteBitstream(SBRInfo *sbr, BitStream *bs, int id_aac, int writeFlag)
{
    if (!sbr || !sbr->sbrPresent) return 0;
    int sendHeader = (!sbr->headerSent || (sbr->frameCount % SBR_HEADER_PERIOD == 0));
    int sbrBits = 1 + (sendHeader ? write_sbr_header(sbr, NULL, 0) : 0) + write_sbr_data(sbr, NULL, id_aac, 0);
    int fillBytes = (4 + sbrBits + 7) / 8, padBits = (fillBytes * 8) - (4 + sbrBits), totalBits = 0;
#define WB(v,n) do { if (writeFlag) PutBit(bs,(v),(n)); totalBits += (n); } while(0)
    WB(ID_FIL, 3); if (fillBytes < 15) WB(fillBytes, 4); else { WB(15, 4); WB(fillBytes - 14, 8); }
    WB(SBR_EXT_TYPE_SBR, 4); WB(sendHeader, 1);
    if (sendHeader) totalBits += write_sbr_header(sbr, writeFlag ? bs : NULL, writeFlag);
    totalBits += write_sbr_data(sbr, writeFlag ? bs : NULL, id_aac, writeFlag);
    if (padBits > 0) WB(0, padBits);
    if (writeFlag) { sbr->headerSent = 1; sbr->frameCount++; }
    return totalBits;
}
