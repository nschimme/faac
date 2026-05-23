/*
 * FAAC - Freeware Advanced Audio Coder
 *
 * HE-AAC v1 Spectral Band Replication (SBR) encoder
 * ISO/IEC 14496-3:2009 §4.6.18
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "sbr.h"
#include "sbr_tables.h"
#include "bitstream.h"
#include "util.h"
#include "coder.h"
#include "cpu_compute.h"

#ifdef HAVE_SSE2
extern void sbr_qmf_64_modulation_sse2(const SBRInfo *sbr, const faac_real * restrict proto,
                                       const faac_real * restrict ovl, faac_real * restrict re,
                                       faac_real * restrict im);
#endif

static void sbr_qmf_64_modulation_scalar(const SBRInfo *sbr, const faac_real * restrict proto,
                                         const faac_real * restrict ovl, faac_real * restrict re,
                                         faac_real * restrict im)
{
    int n, k;
    for (n = 0; n < 128; n++) {
        faac_real un = proto[n] * ovl[639 - n] +
                       proto[n + 128] * ovl[511 - n] +
                       proto[n + 256] * ovl[383 - n] +
                       proto[n + 384] * ovl[255 - n] +
                       proto[n + 512] * ovl[127 - n];
        for (k = 0; k < 64; k++) {
            re[k] += un * sbr->cos_table64T[n][k];
            im[k] += un * sbr->sin_table64T[n][k];
        }
    }
}

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

static int compute_kx(int sampleRate, int bs_start_freq)
{
    int temp = (sampleRate < 32000) ? 3000 : (sampleRate < 64000) ? 4000 : 5000;
    int start_min = ((temp << 7) + (sampleRate >> 1)) / sampleRate;
    int row = (sampleRate <= 16000) ? 0 : (sampleRate <= 22050) ? 1 : (sampleRate <= 24000) ? 2 : (sampleRate <= 32000) ? 3 : (sampleRate <= 64000) ? 4 : 5;
    int kx = start_min + sbr_fs_offset[row][bs_start_freq & 15];
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
        float prod = (float)stop_min;
        int prev = stop_min;
        float base = (float)pow(64.0 / (double)stop_min, 1.0 / 13.0);
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
    if (k2 > 64) k2 = 64;
    if (k2 <= kx) k2 = kx + 1;
    int max_span = (sampleRate <= 32000) ? 48 : (sampleRate <= 44100) ? 35 : 32;
    if (k2 - kx > max_span) k2 = kx + max_span;
    return k2;
}

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
    sbr->noiseBandEdges[0] = kx;
    sbr->noiseBandEdges[1] = k2;
    return n_master;
}

SBRInfo *SBRInit(int channels, int sampleRate, int coreSampleRate, unsigned long bitRate)
{
    SBRInfo *sbr = (SBRInfo *)calloc(1, sizeof(SBRInfo));
    if (!sbr) return NULL;
    sbr->sbrPresent = 1;
    sbr->numChannels = channels;
    sbr->sampleRate = sampleRate;
    sbr->coreSampleRate = coreSampleRate;
    sbr->bs_amp_res = 1;
    unsigned long rate_per_ch = bitRate / channels;
    if (rate_per_ch < 24000) {
        sbr->bs_start_freq = 12;
        sbr->bs_alter_scale = 1;
        sbr->dk = 2;
    } else if (rate_per_ch <= 32000) {
        sbr->bs_start_freq = 15;
        sbr->bs_alter_scale = 1;
        sbr->dk = 2;
    } else {
        sbr->bs_start_freq = 15;
        sbr->bs_alter_scale = 0;
        sbr->dk = 1;
    }
    sbr->bs_stop_freq = 14;
    sbr->kx = compute_kx(sampleRate, sbr->bs_start_freq);
    sbr->k2 = compute_k2(sampleRate, sbr->kx, sbr->bs_stop_freq);
    build_freq_table(sbr);
    for (int k = 0; k < SBR_QMF_BANDS; k++) {
        double phase_step = M_PI * (2 * k + 1) / 128.0;
        for (int n = 0; n < SBR_QMF_FILTER_LEN; n++) {
            double phase = phase_step * (2 * n - 63);
            sbr->cos_table[k][n] = (faac_real)cos(phase);
            sbr->sin_table[k][n] = (faac_real)sin(phase);
        }
    }
    for (int k = 0; k < SBR_QMF_BANDS_64; k++) {
        double phase_step = M_PI * (2 * k + 1) / 256.0;
        for (int n = 0; n < 128; n++) {
            double phase = phase_step * (2 * n - 127);
            double c = cos(phase), s = sin(phase);
            sbr->cos_table64T[n][k] = (faac_real)c;
            sbr->sin_table64T[n][k] = (faac_real)s;
            sbr->cos_table64F[n][k] = (float)c;
            sbr->sin_table64F[n][k] = (float)s;
        }
    }
#ifdef HAVE_SSE2
    CPUCaps caps = get_cpu_caps();
    if (caps & CPU_CAP_SSE2) sbr->qmf_64_mod = (void *)sbr_qmf_64_modulation_sse2;
    else sbr->qmf_64_mod = (void *)sbr_qmf_64_modulation_scalar;
#else
    sbr->qmf_64_mod = (void *)sbr_qmf_64_modulation_scalar;
#endif
    return sbr;
}

void SBREnd(SBRInfo *sbr) { if (sbr) free(sbr); }

void qmf_analysis_slot_complex(const SBRInfo *sbr, const faac_real *slot, faac_real *ovl, faac_real *W_re, faac_real *W_im)
{
    memmove(ovl, ovl + 32, 32 * sizeof(faac_real));
    memcpy(ovl + 32, slot, 32 * sizeof(faac_real));
    for (int k = 0; k < 32; k++) {
        faac_real re = 0, im = 0;
        for (int n = 0; n < 64; n++) {
            faac_real hv = h_sbr_qmf[n] * ovl[SBR_QMF_FILTER_LEN - 1 - n];
            re += hv * sbr->cos_table[k][n];
            im += hv * sbr->sin_table[k][n];
        }
        W_re[k] = re; W_im[k] = im;
    }
}

static void qmf_analysis_64_slot_energy(const SBRInfo *sbr, const faac_real * restrict slot, faac_real * restrict ovl, faac_real * restrict energy, int kx, int k2)
{
    faac_real re[64], im[64];
    memmove(ovl, ovl + 64, (SBR_QMF_OVL_LEN_64 - 64) * sizeof(faac_real));
    memcpy(ovl + SBR_QMF_OVL_LEN_64 - 64, slot, 64 * sizeof(faac_real));
    memset(re, 0, 64 * sizeof(faac_real));
    memset(im, 0, 64 * sizeof(faac_real));
    sbr->qmf_64_mod(sbr, sbr_qmf_window_us640, ovl, re, im);
    memset(energy, 0, 64 * sizeof(faac_real));
    for (int k = kx; k < k2; k++) energy[k] = re[k] * re[k] + im[k] * im[k];
}

void qmf_analysis_64_slot_energy_test(const SBRInfo *sbr, const faac_real * restrict slot, faac_real * restrict ovl, faac_real * restrict energy, int kx, int k2) { qmf_analysis_64_slot_energy(sbr, slot, ovl, energy, kx, k2); }

void SBRAnalysis(SBRInfo *sbr, faac_real *timeDomain[MAX_CHANNELS], int numChannels, int numSamples)
{
    int num_slots = numSamples / SBR_QMF_BANDS_64, kx = sbr->kx, k2 = sbr->k2;
    faac_real bandEnergy64[MAX_CHANNELS][SBR_QMF_BANDS_64], slotEnergy[SBR_QMF_BANDS_64];
    for (int ch = 0; ch < numChannels; ch++) {
        memset(bandEnergy64[ch], 0, sizeof(bandEnergy64[ch]));
        for (int slot = 0; slot < num_slots; slot++) {
            qmf_analysis_64_slot_energy(sbr, timeDomain[ch] + slot * SBR_QMF_BANDS_64, sbr->qmfOvl64[ch], slotEnergy, kx, k2);
            for (int k = kx; k < k2; k++) bandEnergy64[ch][k] += slotEnergy[k];
        }
        float sum_e = 0, sum_log_e = 0;
        int n_sbr = k2 - kx > 0 ? k2 - kx : 1;
        for (int k = kx; k < k2; k++) {
            float enrg = (float)bandEnergy64[ch][k] + 1e-15f;
            sum_e += enrg; sum_log_e += FAAC_LOG(enrg);
        }
        float sfm = FAAC_EXP(sum_log_e / n_sbr) / (sum_e / n_sbr + 1e-15f);
        /* Spectral Flatness Measure for noise floor estimation.
         * sfm = geometric_mean / arithmetic_mean.
         * sfm -> 1.0 (noise), sfm -> 0.0 (tonal). */
        int noise_level = clamp_int((int)(30.0f * sfm), 0, 30);

        for (int e = 0; e < SBR_NUM_ENVELOPES; e++) {
            int prevLevel = -1;
            for (int b = 0; b < sbr->numBands; b++) {
                int k_lo = sbr->bandEdges[b], k_hi = sbr->bandEdges[b+1] > k_lo ? sbr->bandEdges[b+1] : k_lo + 1;
                if (k_hi > SBR_QMF_BANDS_64) k_hi = SBR_QMF_BANDS_64;

                faac_real E = 0;
                for (int k = k_lo; k < k_hi; k++) E += bandEnergy64[ch][k];
                E /= (faac_real)(num_slots * (k_hi - k_lo));

                /* Quantize to 1.5 dB steps (bs_amp_res=1).
                 * level = 2 * log2(E_rel) + offset.
                 * Offset -6.0 is standard-aligned for relative subband energy mapping. */
                int level = (int)lrintf(2.0f * (FAAC_LOG((float)E + 1e-20f) * 1.442695f - 6.0f));
                int raw_level = clamp_int(level, 0, 127);
                if (prevLevel < 0) {
                    sbr->envData[ch][e][b] = raw_level;
                    prevLevel = raw_level;
                } else {
                    int delta = clamp_int(raw_level - prevLevel, -60, 60);
                    sbr->envData[ch][e][b] = delta;
                    prevLevel += delta;
                }
            }
        }
        int prevNoise = -1;
        for (int nb = 0; nb < sbr->numNoiseBands; nb++) {
            if (prevNoise < 0) {
                sbr->noiseData[ch][nb] = noise_level;
                prevNoise = noise_level;
            } else {
                int delta = clamp_int(noise_level - prevNoise, -15, 15);
                sbr->noiseData[ch][nb] = delta; prevNoise += delta;
            }
        }
    }
}

/**
 * Write SBR Header (sbr_header())
 * ISO/IEC 14496-3 Table 4.63
 */
static int write_sbr_header(SBRInfo *sbr, BitStream *bs, int wf)
{
    if (wf) {
        PutBit(bs, sbr->bs_amp_res, 1);
        PutBit(bs, sbr->bs_start_freq, 4);
        PutBit(bs, sbr->bs_stop_freq, 4);
        PutBit(bs, sbr->bs_xover_band, 3);
        PutBit(bs, 0, 2); /* reserved */
        PutBit(bs, 1, 1); /* bs_header_extra_1: present */
        PutBit(bs, 0, 1); /* bs_header_extra_2: not present */

        /* bs_header_extra_1: Table 4.64 */
        PutBit(bs, sbr->bs_alter_scale, 1);
        PutBit(bs, 0, 2); /* bs_noise_bands: 0 = 1 band/octave (matches production) */
        PutBit(bs, 2, 2); /* bs_limiter_bands: 2 = 1.2 bands/octave */
        PutBit(bs, 2, 2); /* bs_limiter_gains: 2 = 2 dB */
        PutBit(bs, 1, 1); /* bs_interpol_freq: 1 = on */
        PutBit(bs, 1, 1); /* bs_smoothing_mode: 1 = on */
    }
    /* 1 + 4 + 4 + 3 + 2 + 1 + 1 (base) + 1 + 2 + 2 + 2 + 1 + 1 (extra_1) = 25 bits */
    return 25;
}

static int write_sbr_grid(SBRInfo *sbr, BitStream *bs, int wf)
{
    if (wf) {
        PutBit(bs, SBR_FRAME_CLASS_FIXFIX, 2);
        PutBit(bs, 0, 2); /* bs_num_env = 0 (1 envelope) */
        PutBit(bs, sbr->bs_amp_res, 1);
    }
    return 5;
}
static int write_sbr_dtdf(BitStream *bs, int wf) { if (wf) { PutBit(bs, 0, 1); PutBit(bs, 0, 1); } return 2; }
static int write_sbr_invf(SBRInfo *sbr, BitStream *bs, int wf) { for (int nb = 0; nb < sbr->numNoiseBands; nb++) { if (wf) PutBit(bs, 2, 2); } return 2 * sbr->numNoiseBands; }

static int write_sbr_envelope(SBRInfo *sbr, BitStream *bs, int ch, int wf)
{
    int bits = 0;
    const SBRHuffEntry *table = sbr->bs_amp_res ? f_huff_env_1_5dB : f_huff_env_3_0dB;
    int nsyms = sbr->bs_amp_res ? F_HUFF_ENV_1_5DB_NSYMS : F_HUFF_ENV_3_0DB_NSYMS;
    int offset = sbr->bs_amp_res ? F_HUFF_ENV_1_5DB_OFFSET : F_HUFF_ENV_3_0DB_OFFSET;

    for (int e = 0; e < SBR_NUM_ENVELOPES; e++) {
        for (int b = 0; b < sbr->numBands; b++) {
            int val = sbr->envData[ch][e][b];
            if (b == 0) {
                int first_bits = sbr->bs_amp_res ? 7 : 6;
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
    for (int nb = 0; nb < sbr->numNoiseBands; nb++) {
        int val = sbr->noiseData[ch][nb];
        if (nb == 0) { if (wf) PutBit(bs, clamp_int(val, 0, 30), 5); bits += 5; }
        else bits += put_huff(bs, f_huff_env_3_0dB, F_HUFF_ENV_3_0DB_NSYMS, F_HUFF_ENV_3_0DB_OFFSET, val, wf);
    }
    return bits;
}

static int write_sbr_data(SBRInfo *sbr, BitStream *bs, int id_aac, int wf)
{
    int bits = 0;
    if (id_aac == ID_CPE) {
        if (wf) {
            PutBit(bs, 0, 1); /* bs_data_extra */
            PutBit(bs, 0, 1); /* bs_coupling */
        }
        bits += 2;
        bits += write_sbr_grid(sbr, bs, wf);
        bits += write_sbr_grid(sbr, bs, wf);
        bits += write_sbr_dtdf(bs, wf);
        bits += write_sbr_dtdf(bs, wf);
        bits += write_sbr_invf(sbr, bs, wf);
        bits += write_sbr_invf(sbr, bs, wf);
        bits += write_sbr_envelope(sbr, bs, 0, wf);
        bits += write_sbr_envelope(sbr, bs, 1, wf);
        bits += write_sbr_noise(sbr, bs, 0, wf);
        bits += write_sbr_noise(sbr, bs, 1, wf);
        if (wf) {
            PutBit(bs, 0, 1); /* bs_add_harmonics */
            PutBit(bs, 0, 1); /* bs_extended_data_present */
        }
        bits += 2;
    } else {
        if (wf) PutBit(bs, 0, 1); /* bs_data_extra */
        bits += 1;
        bits += write_sbr_grid(sbr, bs, wf);
        bits += write_sbr_dtdf(bs, wf);
        bits += write_sbr_invf(sbr, bs, wf);
        bits += write_sbr_envelope(sbr, bs, 0, wf);
        bits += write_sbr_noise(sbr, bs, 0, wf);
        if (wf) {
            PutBit(bs, 0, 1); /* bs_add_harmonics */
            PutBit(bs, 0, 1); /* bs_extended_data_present */
        }
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
