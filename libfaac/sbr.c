#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "sbr.h"
#include "sbr_tables.h"
#include "bitstream.h"
#include "util.h"
#include "coder.h"

static int clamp_int(int x, int lo, int hi) { if (x < lo) return lo; if (x > hi) return hi; return x; }
static int put_huff(BitStream *bs, const SBRHuffEntry *table, int nsyms, int offset, int delta, int writeFlag) {
    int sym = delta + offset; if (sym < 0) sym = 0; if (sym >= nsyms) sym = nsyms - 1;
    if (writeFlag) { PutBit(bs, table[sym].code, table[sym].len); } return table[sym].len;
}
static int compute_kx(int sampleRate, int bs_start_freq) {
    int temp = (sampleRate < 32000) ? 3000 : (sampleRate < 64000) ? 4000 : 5000;
    int start_min = ((temp << 7) + (sampleRate >> 1)) / sampleRate;
    int row = (sampleRate <= 16000) ? 0 : (sampleRate <= 22050) ? 1 : (sampleRate <= 24000) ? 2 : (sampleRate <= 32000) ? 3 : (sampleRate <= 64000) ? 4 : 5;
    int kx = start_min + sbr_fs_offset[row][bs_start_freq & 15];
    if (kx < 1) { kx = 1; } if (kx > 63) { kx = 63; } return kx;
}
static int cmp_int16(const void *a, const void *b) { return (int)(*(const short *)a) - (int)(*(const short *)b); }
static int compute_k2(int sampleRate, int kx, int bs_stop_freq) {
    if (bs_stop_freq == 14 || bs_stop_freq == 15) return 64;
    int temp = (sampleRate < 32000) ? 3000 : (sampleRate < 64000) ? 4000 : 5000;
    int stop_min = ((temp << 8) + (sampleRate >> 1)) / sampleRate; int k2;
    if (bs_stop_freq < 14) {
        short stop_dk[13]; float prod = (float)stop_min; int prev = stop_min;
        float base = powf(64.0f / (float)stop_min, 1.0f / 13.0f);
        for (int i = 0; i < 12; i++) { prod *= base; int present = (int)lrintf(prod); stop_dk[i] = (short)(present - prev); prev = present; }
        stop_dk[12] = (short)(64 - prev); qsort(stop_dk, 13, sizeof(short), cmp_int16);
        k2 = stop_min; for (int i = 0; i < bs_stop_freq; i++) k2 += stop_dk[i];
    } else { k2 = 64; }
    if (k2 > 64) { k2 = 64; } if (k2 <= kx) { k2 = kx + 1; }
    int max_span = (sampleRate <= 32000) ? 48 : (sampleRate <= 44100) ? 35 : 32;
    if (k2 - kx > max_span) { k2 = kx + max_span; } return k2;
}
static int build_freq_table(SBRInfo *sbr) {
    int kx = sbr->kx, k2 = sbr->k2, dk = sbr->dk;
    int n_master = ((k2 - kx + (dk & 2)) >> dk) << 1;
    if (n_master < 1) { n_master = 1; } if (n_master > 64) { n_master = 64; }
    int f_master[64 + 1]; for (int k = 1; k <= n_master; k++) f_master[k] = dk;
    int k2diff = (k2 - kx) - n_master * dk;
    if (k2diff < 0) { f_master[1]--; if (k2diff < -1) f_master[2]--; } else if (k2diff > 0) f_master[n_master]++;
    f_master[0] = kx; for (int k = 1; k <= n_master; k++) f_master[k] += f_master[k - 1];
    sbr->numBands = n_master; for (int b = 0; b <= n_master; b++) sbr->bandEdges[b] = f_master[b];
    sbr->numNoiseBands = 1; sbr->noiseBandEdges[0] = kx; sbr->noiseBandEdges[1] = k2; return n_master;
}
SBRInfo *SBRInit(int channels, int sampleRate, int coreSampleRate, unsigned long bitRate) {
    SBRInfo *sbr = (SBRInfo *)calloc(1, sizeof(SBRInfo)); if (!sbr) return NULL;
    sbr->sbrPresent = 1; sbr->numChannels = channels; sbr->sampleRate = sampleRate; sbr->coreSampleRate = coreSampleRate;
    unsigned long rate_per_ch = bitRate / channels; sbr->bs_amp_res = (rate_per_ch < 20000) ? 0 : 1;
    if (rate_per_ch < 24000) { sbr->bs_start_freq = 12; sbr->bs_alter_scale = 1; sbr->dk = 2; }
    else if (rate_per_ch <= 32000) { sbr->bs_start_freq = 15; sbr->bs_alter_scale = 1; sbr->dk = 2; }
    else { sbr->bs_start_freq = 15; sbr->bs_alter_scale = 0; sbr->dk = 1; }
    sbr->bs_stop_freq = 10; sbr->bs_freq_res = 1; sbr->numEnvelopes = 1; sbr->transientThresh = 16.0f;
    sbr->eff_amp_res = (sbr->numEnvelopes == 1) ? 0 : sbr->bs_amp_res;
    sbr->kx = compute_kx(sampleRate, sbr->bs_start_freq); sbr->k2 = compute_k2(sampleRate, sbr->kx, sbr->bs_stop_freq);
    build_freq_table(sbr);
    for (int n = 0; n < 128; n++) {
        double phase = M_PI * n / 128.0; sbr->twidCos[n] = (faac_real)cos(phase); sbr->twidSin[n] = (faac_real)sin(phase);
    }
    fft_initialize(&sbr->fftTables); return sbr;
}
void SBREnd(SBRInfo *sbr) { if (!sbr) return; fft_terminate(&sbr->fftTables); free(sbr); }
static inline float fast_log2(float x) {
    union { float f; int32_t i; } vx; vx.f = x;
    int32_t exp = (vx.i >> 23) & 0xFF; float m = (float)(vx.i & 0x7FFFFF) * (1.0f / 8388608.0f);
    return (float)(exp - 127) + (m * (1.3424f - 0.3427f * m));
}
static void qmf_analysis_64_slot_energy_fft(const SBRInfo *sbr, const faac_real * restrict ovl_pos, faac_real * restrict energy, int kx, int k2) {
    faac_real xr[128], xi[128]; const faac_real * restrict proto = sbr_qmf_window_us640;
    const faac_real * restrict twidCos = sbr->twidCos; const faac_real * restrict twidSin = sbr->twidSin;
    const faac_real * restrict p_ovl = ovl_pos + 639;
    for (int n = 0; n < 128; n++) {
        faac_real un = proto[n] * p_ovl[0] + proto[n+128] * p_ovl[-128] + proto[n+256] * p_ovl[-256] + proto[n+384] * p_ovl[-384] + proto[n+512] * p_ovl[-512];
        xr[n] = un * (*twidCos++); xi[n] = -un * (*twidSin++); p_ovl--;
    }
    fft((FFT_Tables *)&sbr->fftTables, xr, xi, 7);
    for (int k = kx; k < k2; k++) { energy[k] = xr[k] * xr[k] + xi[k] * xi[k]; }
}
void SBRAnalysis(SBRInfo *sbr, faac_real *timeDomain[MAX_CHANNELS], int numChannels, int numSamples) {
    int num_slots = numSamples / 64, kx = sbr->kx, k2 = sbr->k2;
    int nch = clamp_int(numChannels, 1, 2); int half_slots = num_slots / 2 > 0 ? num_slots / 2 : 1;
    faac_real bandHalfE[2][2][64]; faac_real slotEnergy[64]; float tratio = 0.0f;
    faac_real workspace[576 + 2048]; int sampled = 0;
    memset(bandHalfE, 0, sizeof(bandHalfE));
    for (int ch = 0; ch < nch; ch++) {
        float smax = 0.0f, ssum = 0.0f; sampled = 0;
        memcpy(workspace, sbr->qmfOvl64[ch], 576 * sizeof(faac_real));
        memcpy(workspace + 576, timeDomain[ch], numSamples * sizeof(faac_real));
        for (int slot = 0; slot < num_slots; slot++) {
            faac_real *ovl_pos = workspace + slot * 64;
            const faac_real * __restrict p_in = timeDomain[ch] + slot * 64;
            float stot = 0.0f; for (int n = 0; n < 64; n++) { float val = (float)*p_in++; stot += val * val; }
            if (stot > smax) { smax = stot; } ssum += stot;
            if (!(slot & 3)) {
                qmf_analysis_64_slot_energy_fft(sbr, ovl_pos, slotEnergy, kx, k2); sampled++;
                int h = clamp_int(slot / half_slots, 0, 1);
                for (int k = kx; k < k2; k++) { bandHalfE[ch][h][k] += slotEnergy[k]; }
            }
        }
        float ratio = smax * (float)num_slots / (ssum + 1e-15f); if (ratio > tratio) tratio = ratio;
        memcpy(sbr->qmfOvl64[ch], workspace + numSamples, 576 * sizeof(faac_real));
    }
    sbr->numEnvelopes = (tratio > sbr->transientThresh) ? 2 : 1;
    sbr->eff_amp_res = (sbr->numEnvelopes == 1) ? 0 : sbr->bs_amp_res;
    for (int ch = 0; ch < nch; ch++) {
        sbr->invfMode[ch] = 3; int dlav = sbr->eff_amp_res ? 31 : 60;
        for (int e = 0; e < sbr->numEnvelopes; e++) {
            int prevLevel = -1;
            for (int b = 0; b < sbr->numBands; b++) {
                int k_lo = sbr->bandEdges[b], k_hi = sbr->bandEdges[b+1];
                int e_slots = (sbr->numEnvelopes == 1) ? sampled : sampled / 2; if (e_slots < 1) e_slots = 1;
                faac_real E = 0; if (sbr->numEnvelopes == 1) { for (int k = k_lo; k < k_hi; k++) E += bandHalfE[ch][0][k] + bandHalfE[ch][1][k]; }
                else { for (int k = k_lo; k < k_hi; k++) E += bandHalfE[ch][e][k]; }
                E /= (faac_real)(e_slots * (k_hi - k_lo));
                float factor = sbr->eff_amp_res ? 1.0f : 2.0f;
                int level = (int)lrintf(factor * (fast_log2((float)E + 1e-20f) - 6.0f));
                int raw_level = clamp_int(level, 0, 127);
                if (prevLevel < 0) { raw_level = clamp_int(raw_level, 0, sbr->eff_amp_res ? 63 : 127); sbr->envData[ch][e][b] = raw_level; prevLevel = raw_level; }
                else { int delta = clamp_int(raw_level - prevLevel, -dlav, dlav); sbr->envData[ch][e][b] = delta; prevLevel += delta; }
            }
        }
        int prevNoise = -1; for (int nb = 0; nb < 1; nb++) {
            if (prevNoise < 0) { sbr->noiseData[ch][nb] = 0; prevNoise = 0; }
            else { int delta = clamp_int(0 - prevNoise, -15, 15); sbr->noiseData[ch][nb] = delta; prevNoise += delta; }
        }
    }
}
static int write_sbr_header(SBRInfo *sbr, BitStream *bs, int wf) {
    if (wf) { PutBit(bs, sbr->bs_amp_res, 1); PutBit(bs, sbr->bs_start_freq, 4); PutBit(bs, sbr->bs_stop_freq, 4); PutBit(bs, sbr->bs_xover_band, 3);
        PutBit(bs, 0, 2); PutBit(bs, 1, 1); PutBit(bs, 1, 1);
        PutBit(bs, 0, 2); PutBit(bs, sbr->bs_alter_scale, 1); PutBit(bs, 0, 2);
        PutBit(bs, 2, 2); PutBit(bs, 2, 2); PutBit(bs, 1, 1); PutBit(bs, 1, 1); }
    return 27;
}
static int write_sbr_grid(SBRInfo *sbr, BitStream *bs, int wf) {
    int n_env = sbr->numEnvelopes > 1 ? 1 : 0;
    if (wf) { PutBit(bs, 0, 2); PutBit(bs, n_env, 2); for (int i = 0; i <= n_env; i++) PutBit(bs, sbr->bs_freq_res, 1); }
    return 4 + (n_env + 1);
}
static int write_sbr_dtdf(SBRInfo *sbr, BitStream *bs, int wf) {
    int n_q = sbr->numEnvelopes > 1 ? 2 : 1; int bits = sbr->numEnvelopes + n_q;
    if (wf) { for (int i = 0; i < bits; i++) PutBit(bs, 0, 1); } return bits;
}
static int write_sbr_invf(SBRInfo *sbr, BitStream *bs, int ch, int wf) { for (int nb = 0; nb < 1; nb++) { if (wf) PutBit(bs, sbr->invfMode[ch], 2); } return 2; }
static int write_sbr_envelope(SBRInfo *sbr, BitStream *bs, int ch, int wf) {
    int bits = 0; const SBRHuffEntry *table = sbr->eff_amp_res ? f_huff_env_3_0dB : f_huff_env_1_5dB;
    int nsyms = sbr->eff_amp_res ? 63 : 121; int offset = sbr->eff_amp_res ? 31 : 60;
    for (int e = 0; e < sbr->numEnvelopes; e++) {
        for (int b = 0; b < sbr->numBands; b++) {
            int val = sbr->envData[ch][e][b];
            if (b == 0) { int first_bits = sbr->eff_amp_res ? 6 : 7; if (wf) PutBit(bs, clamp_int(val, 0, (1 << first_bits) - 1), first_bits); bits += first_bits; }
            else { bits += put_huff(bs, table, nsyms, offset, val, wf); }
        }
    } return bits;
}
static int write_sbr_noise(SBRInfo *sbr, BitStream *bs, int ch, int wf) {
    int bits = 0; int n_q = sbr->numEnvelopes > 1 ? 2 : 1;
    for (int ne = 0; ne < n_q; ne++) {
        for (int nb = 0; nb < 1; nb++) {
            int val = sbr->noiseData[ch][nb];
            if (nb == 0) { if (wf) PutBit(bs, clamp_int(val, 0, 30), 5); bits += 5; }
            else bits += put_huff(bs, f_huff_env_3_0dB, 63, 31, val, wf);
        }
    } return bits;
}
static int write_sbr_data(SBRInfo *sbr, BitStream *bs, int id_aac, int wf) {
    int bits = 0; if (id_aac == ID_CPE) { if (wf) { PutBit(bs, 0, 1); PutBit(bs, 0, 1); } bits += 2;
        bits += write_sbr_grid(sbr, bs, wf); bits += write_sbr_grid(sbr, bs, wf);
        bits += write_sbr_dtdf(sbr, bs, wf); bits += write_sbr_dtdf(sbr, bs, wf);
        bits += write_sbr_invf(sbr, bs, 0, wf); bits += write_sbr_invf(sbr, bs, 1, wf);
        bits += write_sbr_envelope(sbr, bs, 0, wf); bits += write_sbr_envelope(sbr, bs, 1, wf);
        bits += write_sbr_noise(sbr, bs, 0, wf); bits += write_sbr_noise(sbr, bs, 1, wf);
        if (wf) { PutBit(bs, 0, 1); PutBit(bs, 0, 1); PutBit(bs, 0, 1); } bits += 3;
    } else { if (wf) { PutBit(bs, 0, 1); } bits += 1;
        bits += write_sbr_grid(sbr, bs, wf); bits += write_sbr_dtdf(sbr, bs, wf);
        bits += write_sbr_invf(sbr, bs, 0, wf); bits += write_sbr_envelope(sbr, bs, 0, wf);
        bits += write_sbr_noise(sbr, bs, 0, wf); if (wf) { PutBit(bs, 0, 1); PutBit(bs, 0, 1); } bits += 2;
    } return bits;
}
int SBRWriteBitstream(SBRInfo *sbr, BitStream *bs, int id_aac, int writeFlag) {
    if (!sbr || !sbr->sbrPresent) return 0;
    int sendHeader = (!sbr->headerSent || (sbr->frameCount % 30 == 0));
    int sbrBits = 1 + (sendHeader ? write_sbr_header(sbr, NULL, 0) : 0) + write_sbr_data(sbr, NULL, id_aac, 0);
    int fillBytes = (4 + sbrBits + 7) / 8, padBits = (fillBytes * 8) - (4 + sbrBits), totalBits = 0;
#define WB(v,n) do { if (writeFlag) PutBit(bs,(v),(n)); totalBits += (n); } while(0)
    WB(ID_FIL, 3); if (fillBytes < 15) { WB(fillBytes, 4); } else { WB(15, 4); WB(fillBytes - 14, 8); }
    WB(SBR_EXT_TYPE_SBR, 4); WB(sendHeader, 1);
    if (sendHeader) totalBits += write_sbr_header(sbr, writeFlag ? bs : NULL, writeFlag);
    totalBits += write_sbr_data(sbr, writeFlag ? bs : NULL, id_aac, writeFlag);
    if (padBits > 0) { WB(0, padBits); } if (writeFlag) { sbr->headerSent = 1; sbr->frameCount++; } return totalBits;
}
void qmf_analysis_64_slot_energy_test(const SBRInfo *sbr, const faac_real * restrict slot, faac_real * restrict ovl, faac_real * restrict energy, int kx, int k2) {
  faac_real workspace[576 + 64]; memcpy(workspace, ovl, 576*sizeof(faac_real)); memcpy(workspace+576, slot, 64*sizeof(faac_real));
  qmf_analysis_64_slot_energy_fft(sbr, workspace, energy, kx, k2); memcpy(ovl, workspace+64, 576*sizeof(faac_real));
}
void qmf_analysis_slot_complex(const SBRInfo *sbr, const faac_real *slot, faac_real *ovl, faac_real *W_re, faac_real *W_im) {
  faac_real energy[64];
  faac_real workspace[576 + 64]; memcpy(workspace, ovl, 576*sizeof(faac_real)); memcpy(workspace+576, slot, 64*sizeof(faac_real));
  qmf_analysis_64_slot_energy_fft(sbr, workspace, energy, 0, 64);
  for(int k=0; k<64; k++) { W_re[k] = sqrtf(energy[k]); W_im[k] = 0; }
  memcpy(ovl, workspace+64, 576*sizeof(faac_real));
}
