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
    if (writeFlag) {
        PutBit(bs, table[sym].code, table[sym].len);
    }
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
    if (bs_stop_freq == 14) return 2 * kx > 64 ? 64 : 2 * kx;
    if (bs_stop_freq == 15) return 3 * kx > 64 ? 64 : 3 * kx;

    int temp = (sampleRate < 32000) ? 3000 : (sampleRate < 64000) ? 4000 : 5000;
    int stop_min = ((temp << 8) + (sampleRate >> 1)) / sampleRate;
    int k2;

    if (bs_stop_freq < 14) {
        short stop_dk[13];
        float prod = (float)stop_min;
        int prev = stop_min;
        int i;
        float base = (float)pow(64.0 / (double)stop_min, 1.0 / 13.0);
        for (i = 0; i < 12; i++) {
            prod *= base;
            int present = (int)lrintf(prod);
            stop_dk[i] = (short)(present - prev);
            prev = present;
        }
        stop_dk[12] = (short)(64 - prev);
        qsort(stop_dk, 13, sizeof(short), cmp_int16);
        k2 = stop_min;
        for (i = 0; i < bs_stop_freq; i++) {
            k2 += stop_dk[i];
        }
    } else {
        k2 = 64;
    }

    if (k2 > 64) k2 = 64;
    if (k2 <= kx) k2 = kx + 1;

    int max_span = (sampleRate <= 32000) ? 48 : (sampleRate <= 44100) ? 35 : 32;
    if (k2 - kx > max_span) {
        k2 = kx + max_span;
    }

    return k2;
}

static int build_freq_table(SBRInfo *sbr)
{
    /* Use standard dk=1 for stable frequency grid resolution. */
    int kx = sbr->kx, k2 = sbr->k2, dk = 1;
    int n_master = ((k2 - kx + (dk & 2)) >> dk) << 1;
    int k;

    if (n_master < 1) n_master = 1;
    if (n_master > SBR_MAX_BANDS) n_master = SBR_MAX_BANDS;

    int f_master[SBR_MAX_BANDS + 1];
    for (k = 1; k <= n_master; k++) {
        f_master[k] = dk;
    }

    int k2diff = (k2 - kx) - n_master * dk;
    if (k2diff < 0) {
        f_master[1]--;
        if (k2diff < -1) f_master[2]--;
    } else if (k2diff > 0) {
        f_master[n_master]++;
    }

    f_master[0] = kx;
    for (k = 1; k <= n_master; k++) {
        f_master[k] += f_master[k - 1];
    }

    sbr->numBands = n_master;
    for (int b = 0; b <= n_master; b++) {
        sbr->bandEdges[b] = f_master[b];
    }

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
    sbr->headerSent = 0;
    sbr->frameCount = 0;
    sbr->numChannels = channels;
    sbr->sampleRate = sampleRate;
    sbr->coreSampleRate = coreSampleRate;

    sbr->bs_amp_res = 0;
    sbr->bs_start_freq = 15;
    sbr->bs_stop_freq = 14;
    sbr->bs_xover_band = 0;
    sbr->bs_alter_scale = 0;

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
            double c = cos(phase);
            double s = sin(phase);
            sbr->cos_table64T[n][k] = (faac_real)c;
            sbr->sin_table64T[n][k] = (faac_real)s;
            sbr->cos_table64F[n][k] = (float)c;
            sbr->sin_table64F[n][k] = (float)s;
        }
    }

#ifdef HAVE_SSE2
    CPUCaps caps = get_cpu_caps();
    if (caps & CPU_CAP_SSE2) {
        sbr->qmf_64_mod = (void *)sbr_qmf_64_modulation_sse2;
    } else {
        sbr->qmf_64_mod = (void *)sbr_qmf_64_modulation_scalar;
    }
#else
    sbr->qmf_64_mod = (void *)sbr_qmf_64_modulation_scalar;
#endif

    memset(sbr->qmfOvl64, 0, sizeof(sbr->qmfOvl64));
    return sbr;
}

void SBREnd(SBRInfo *sbr)
{
    if (sbr) {
        free(sbr);
    }
}

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
        W_re[k] = re;
        W_im[k] = im;
    }
}

static void qmf_analysis_64_slot_energy(const SBRInfo *sbr, const faac_real * restrict slot, faac_real * restrict ovl, faac_real * restrict energy, int kx, int k2)
{
    int k;
    faac_real re[64], im[64];
    const faac_real * restrict proto = sbr_qmf_window_us640;

    memmove(ovl, ovl + 64, (SBR_QMF_OVL_LEN_64 - 64) * sizeof(faac_real));
    memcpy(ovl + SBR_QMF_OVL_LEN_64 - 64, slot, 64 * sizeof(faac_real));

    memset(re, 0, 64 * sizeof(faac_real));
    memset(im, 0, 64 * sizeof(faac_real));

    sbr->qmf_64_mod(sbr, proto, ovl, re, im);

    memset(energy, 0, 64 * sizeof(faac_real));
    for (k = kx; k < k2; k++) {
        energy[k] = re[k] * re[k] + im[k] * im[k];
    }
}

void qmf_analysis_64_slot_energy_test(const SBRInfo *sbr, const faac_real * restrict slot, faac_real * restrict ovl, faac_real * restrict energy, int kx, int k2)
{
    qmf_analysis_64_slot_energy(sbr, slot, ovl, energy, kx, k2);
}

void SBRAnalysis(SBRInfo *sbr, faac_real *timeDomain[MAX_CHANNELS], int numChannels, int numSamples)
{
    int num_slots = numSamples / SBR_QMF_BANDS_64;
    int kx = sbr->kx, k2 = sbr->k2;
    faac_real bandEnergy64[MAX_CHANNELS][SBR_QMF_BANDS_64];
    faac_real slotEnergy[SBR_QMF_BANDS_64];
    int ch, slot, k, e, b, nb;

    for (ch = 0; ch < numChannels; ch++) {
        memset(bandEnergy64[ch], 0, sizeof(bandEnergy64[ch]));
        for (slot = 0; slot < num_slots; slot++) {
            qmf_analysis_64_slot_energy(sbr, timeDomain[ch] + slot * SBR_QMF_BANDS_64, sbr->qmfOvl64[ch], slotEnergy, kx, k2);
            for (k = kx; k < k2; k++) {
                bandEnergy64[ch][k] += slotEnergy[k];
            }
        }

        float sum_e = 0, sum_log_e = 0;
        int n_sbr = k2 - kx;
        if (n_sbr < 1) n_sbr = 1;

        for (k = kx; k < k2; k++) {
            float enrg = (float)bandEnergy64[ch][k] + 1e-15f;
            sum_e += enrg;
            sum_log_e += FAAC_LOG(enrg);
        }

        float sfm = FAAC_EXP(sum_log_e / n_sbr) / (sum_e / n_sbr + 1e-15f);
        int noise_level = (int)(24.0f * sfm);
        noise_level = clamp_int(noise_level, 0, 30);

        for (e = 0; e < SBR_NUM_ENVELOPES; e++) {
            int prevLevel = -1;
            for (b = 0; b < sbr->numBands; b++) {
                int k_lo = sbr->bandEdges[b];
                int k_hi = sbr->bandEdges[b+1];
                if (k_hi <= k_lo) k_hi = k_lo + 1;
                if (k_hi > SBR_QMF_BANDS_64) k_hi = SBR_QMF_BANDS_64;

                faac_real E = 0;
                for (k = k_lo; k < k_hi; k++) {
                    E += bandEnergy64[ch][k];
                }
                E /= (faac_real)(num_slots * (k_hi - k_lo));

                float log2E = FAAC_LOG((float)E + 1e-20f) * 1.4426950408889634f;
                int level = (int)lrintf(2.0f * (log2E + 24.0f));

                if (prevLevel < 0) {
                    level = clamp_int(level, 0, 127);
                    sbr->envData[ch][e][b] = level;
                } else {
                    int raw_level = clamp_int(level, 0, 127);
                    int delta = clamp_int(raw_level - prevLevel, -60, 60);
                    sbr->envData[ch][e][b] = delta;
                    level = prevLevel + delta;
                }
                prevLevel = level;
            }
        }

        int prevNoise = -1;
        for (nb = 0; nb < sbr->numNoiseBands; nb++) {
            int level = noise_level;
            if (prevNoise < 0) {
                sbr->noiseData[ch][nb] = level;
            } else {
                int delta = clamp_int(level - prevNoise, -15, 15);
                sbr->noiseData[ch][nb] = delta;
                level = prevNoise + delta;
            }
            prevNoise = level;
        }
    }
}

static int write_sbr_header(SBRInfo *sbr, BitStream *bs, int wf)
{
    int bits = 0;
#define WB(v,n) do { if (wf) PutBit(bs,(v),(n)); bits += (n); } while(0)
    WB(sbr->bs_amp_res, 1);
    WB(sbr->bs_start_freq, 4);
    WB(sbr->bs_stop_freq, 4);
    WB(sbr->bs_xover_band, 3);
    WB(0, 2); // bs_reserved
    WB(1, 1); // bs_header_extra_1
    WB(0, 1); // bs_header_extra_2

    WB(0, 2); // bs_freq_scale
    WB(sbr->bs_alter_scale, 1);
    WB(0, 2); // bs_noise_bands
#undef WB
    return bits;
}

static int write_sbr_grid(BitStream *bs, int wf)
{
    int bits = 0;
    if (wf) {
        PutBit(bs, SBR_FRAME_CLASS_FIXFIX, 2);
        PutBit(bs, 0, 2); // bs_num_env = 1
        PutBit(bs, 1, 1); // bs_freq_res[0] = 1 (HIGH)
    }
    bits += 5;
    return bits;
}

static int write_sbr_dtdf(BitStream *bs, int wf)
{
    int bits = 0;
    if (wf) {
        PutBit(bs, 0, 1); // bs_df_env
        PutBit(bs, 0, 1); // bs_df_noise
    }
    bits += 2;
    return bits;
}

static int write_sbr_invf(SBRInfo *sbr, BitStream *bs, int wf)
{
    int bits = 0;
    int nb;
    for (nb = 0; nb < sbr->numNoiseBands; nb++) {
        if (wf) {
            PutBit(bs, 2, 2);
        }
        bits += 2;
    }
    return bits;
}

static int write_sbr_envelope(SBRInfo *sbr, BitStream *bs, int ch, int wf)
{
    int bits = 0;
    int e, b;
    for (e = 0; e < SBR_NUM_ENVELOPES; e++) {
        for (b = 0; b < sbr->numBands; b++) {
            int val = sbr->envData[ch][e][b];
            if (b == 0) {
                int abs_val = clamp_int(val, 0, 127);
                if (wf) PutBit(bs, abs_val, 7);
                bits += 7;
            } else {
                bits += put_huff(bs, f_huff_env_1_5dB, F_HUFF_ENV_1_5DB_NSYMS, F_HUFF_ENV_1_5DB_OFFSET, val, wf);
            }
        }
    }
    return bits;
}

static int write_sbr_noise(SBRInfo *sbr, BitStream *bs, int ch, int wf)
{
    int bits = 0;
    int nb;
    for (nb = 0; nb < sbr->numNoiseBands; nb++) {
        int val = sbr->noiseData[ch][nb];
        if (nb == 0) {
            int abs_val = clamp_int(val, 0, 30);
            if (wf) PutBit(bs, abs_val, 5);
            bits += 5;
        } else {
            bits += put_huff(bs, f_huff_env_3_0dB, F_HUFF_ENV_3_0DB_NSYMS, F_HUFF_ENV_3_0DB_OFFSET, val, wf);
        }
    }
    return bits;
}

static int write_sbr_data(SBRInfo *sbr, BitStream *bs, int id_aac, int wf)
{
    int bits = 0;
    if (id_aac == ID_CPE) {
        if (wf) {
            PutBit(bs, 0, 1); // bs_data_extra = 0
            PutBit(bs, 0, 1); // bs_coupling = 0
        }
        bits += 2;
        bits += write_sbr_grid(bs, wf);
        bits += write_sbr_grid(bs, wf);
        bits += write_sbr_dtdf(bs, wf);
        bits += write_sbr_dtdf(bs, wf);
        bits += write_sbr_invf(sbr, bs, wf);
        bits += write_sbr_invf(sbr, bs, wf);
        bits += write_sbr_envelope(sbr, bs, 0, wf);
        bits += write_sbr_envelope(sbr, bs, 1, wf);
        bits += write_sbr_noise(sbr, bs, 0, wf);
        bits += write_sbr_noise(sbr, bs, 1, wf);
        if (wf) {
            PutBit(bs, 0, 1); // bs_add_harmonic_flag L
            PutBit(bs, 0, 1); // bs_add_harmonic_flag R
            PutBit(bs, 0, 1); // bs_extended_data = 0
        }
        bits += 3;
    } else {
        if (wf) {
            PutBit(bs, 0, 1); // bs_data_extra = 0
        }
        bits += 1;
        bits += write_sbr_grid(bs, wf);
        bits += write_sbr_dtdf(bs, wf);
        bits += write_sbr_invf(sbr, bs, wf);
        bits += write_sbr_envelope(sbr, bs, 0, wf);
        bits += write_sbr_noise(sbr, bs, 0, wf);
        if (wf) {
            PutBit(bs, 0, 1); // bs_add_harmonic_flag
            PutBit(bs, 0, 1); // bs_extended_data = 0
        }
        bits += 2;
    }
    return bits;
}

int SBRWriteBitstream(SBRInfo *sbr, BitStream *bs, int id_aac, int writeFlag)
{
    if (!sbr || !sbr->sbrPresent) return 0;
    int sendHeader = (!sbr->headerSent || (sbr->frameCount % SBR_HEADER_PERIOD == 0));

    int sbrBits = 1; // bs_header_flag
    if (sendHeader) sbrBits += write_sbr_header(sbr, NULL, 0);
    sbrBits += write_sbr_data(sbr, NULL, id_aac, 0);

    int payloadBits = 4 + sbrBits; // extension_type (4) + SBR data
    int fillBytes = (payloadBits + 7) / 8;
    int padBits = (fillBytes * 8) - payloadBits;

    int totalBits = 0;
#define WB(v,n) do { if (writeFlag) PutBit(bs,(v),(n)); totalBits += (n); } while(0)
    WB(ID_FIL, 3);
    if (fillBytes < 15) {
        WB(fillBytes, 4);
    } else {
        WB(15, 4);
        WB(fillBytes - 14, 8);
    }

    WB(SBR_EXT_TYPE_SBR, 4);
    WB(sendHeader ? 1 : 0, 1);
    if (sendHeader) {
        totalBits += write_sbr_header(sbr, writeFlag ? bs : NULL, writeFlag);
    }
    totalBits += write_sbr_data(sbr, writeFlag ? bs : NULL, id_aac, writeFlag);
    if (padBits > 0) {
        WB(0, padBits);
    }
#undef WB

    if (writeFlag) {
        sbr->headerSent = 1;
        sbr->frameCount++;
    }
    return totalBits;
}
