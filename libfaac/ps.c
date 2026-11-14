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

#include <math.h>
#ifdef FAAC_STATS
#include <stdio.h>
#include "stats.h"
#endif

#include "ps.h"
#include "ps_huff_tables.h"
#include "sbr_internal.h"
#include "sbr_tables.h"
#include "bitstream.h"
#include "util.h"
#include "fft.h"

#define SBR_PS_BANDS PS_BANDS
#define PHASE_BANDS PS_PHASE_BANDS
#define SBR_PS_ICC_LEVELS 8
#define SBR_PS_ICC_MAX_INDEX 7
#define SBR_EXT_ID_PS 2
#define PS_HUFF_ICC_NSYMS 15
#define PS_HUFF_ICC_OFFSET 7
/* A NULL-buffer stream only counts, so sizing runs the same writer as output. */
static void ps_counter(BitStream *bs)
{
    InitBitStream(bs, NULL, 4096);
}

static int put_huff(BitStream *bs, const PsHuffPackedEntry *table, int nsyms, int offset,
                    int delta)
{
    int sym = clamp_int(delta + offset, 0, nsyms - 1);
    int len = table[sym].byte[0] & 31;
    uint32_t packed = (uint32_t)table[sym].byte[0] | ((uint32_t)table[sym].byte[1] << 8) |
                      ((uint32_t)table[sym].byte[2] << 16);
    PutBit(bs, packed >> 5, len);
    return len;
}

static int ps_param_bits(BitStream *bs, const int8_t *cur, const int8_t *prev, int bands, int wrap,
                         const PsHuffPackedEntry *table, int nsyms, int offset)
{
    int bits = 0, ref = 0;
    for (int b = 0; b < bands; b++) {
        int delta = cur[b] - (prev ? prev[b] : ref);
        if (wrap)
            delta &= 7;
        bits += put_huff(bs, table, nsyms, offset, delta);
        ref = cur[b];
    }
    return bits;
}

/* Use temporal deltas only when they shorten the payload; headers stay self-contained. */
static int write_ps_params(BitStream *bs, const int8_t *cur, const int8_t *prev, int bands, int wrap,
                           const PsHuffPackedEntry *df, const PsHuffPackedEntry *dt, int nsyms,
                           int offset)
{
    BitStream count;
    ps_counter(&count);
    int freq = ps_param_bits(&count, cur, NULL, bands, wrap, df, nsyms, offset);
    int time = freq;
    if (prev) {
        ps_counter(&count);
        time = ps_param_bits(&count, cur, prev, bands, wrap, dt, nsyms, offset);
    }
    bool temporal = prev && time < freq;
    PutBit(bs, temporal, 1);
    return 1 + ps_param_bits(bs, cur, temporal ? prev : NULL, bands, wrap, temporal ? dt : df,
                             nsyms, offset);
}

static int count_ps_param_bits(const int8_t *cur, const int8_t *prev, int bands, int wrap,
                               const PsHuffPackedEntry *df, const PsHuffPackedEntry *dt, int nsyms,
                               int offset)
{
    BitStream count;
    ps_counter(&count);
    return write_ps_params(&count, cur, prev, bands, wrap, df, dt, nsyms, offset);
}

/* The low three QMF bands need finer stereo resolution (ISO/IEC 14496-3 8.6.4.3).
 * Six slots of look-ahead center the 13-tap bank. */
void PsHybridAnalyze(PsHybrid *state, float in[2][3][44][2], float energy[2][4][64],
                     float cross[4][64], float crossImag[4][64], int slots)
{
    static const float g8[7] = {.00746082949812f,
                                .02270420949825f,
                                .04546865930473f,
                                .07266113929591f,
                                .09885108575264f,
                                .11793710567217f,
                                .125f};
    static const float g2[7] = {0, .01899487526049f, 0,  -.07293139167538f,
                                0, .30596630545168f, .5f};
    static const unsigned char map[10] = {1, 0, 0, 1, 2, 3, 4, 5, 6, 7};
    if (!state->initialized) {
        for (int q = 0; q < 8; q++)
            for (int j = 0; j < 7; j++) {
                double phase = -2.0 * 3.14159265358979323846 * (q + .5) * (j - 6) / 8;
                state->coef[q][j][0] = (float)(g8[j] * cos(phase));
                state->coef[q][j][1] = (float)(g8[j] * sin(phase));
            }
        state->initialized = 1;
    }
    for (int n = 0; n < slots; n += FAAC_SBR_DECIMATION) {
        float out[2][10][2];
        for (int ch = 0; ch < 2; ch++) {
            float t[8][2];
            for (int q = 0; q < 8; q++) {
                float sr = state->coef[q][6][0] * in[ch][0][n + 6][0];
                float si = state->coef[q][6][0] * in[ch][0][n + 6][1];
                for (int j = 0; j < 6; j++) {
                    float fr = state->coef[q][j][0], fi = state->coef[q][j][1];
                    float ar = in[ch][0][n + j][0], ai = in[ch][0][n + j][1];
                    float br = in[ch][0][n + 12 - j][0], bi = in[ch][0][n + 12 - j][1];
                    sr += fr * (ar + br) - fi * (ai - bi);
                    si += fr * (ai + bi) + fi * (ar - br);
                }
                t[q][0] = sr;
                t[q][1] = si;
            }
            for (int i = 0; i < 2; i++) {
                out[ch][0][i] = t[6][i];
                out[ch][1][i] = t[7][i];
                out[ch][2][i] = t[0][i];
                out[ch][3][i] = t[1][i];
                out[ch][4][i] = t[2][i] + t[5][i];
                out[ch][5][i] = t[3][i] + t[4][i];
            }
            for (int k = 1; k < 3; k++) {
                int reverse = k == 1;
                for (int i = 0; i < 2; i++) {
                    float c = g2[6] * in[ch][k][n + 6][i], sum = 0;
                    for (int j = 1; j < 6; j += 2)
                        sum += g2[j] * (in[ch][k][n + j][i] + in[ch][k][n + 12 - j][i]);
                    out[ch][6 + 2 * (k - 1) + reverse][i] = c + sum;
                    out[ch][6 + 2 * (k - 1) + !reverse][i] = c - sum;
                }
            }
        }
        int e = n * 4 / slots;
        for (int k = 0; k < 10; k++) {
            int b = map[k];
            float lr = out[0][k][0], li = out[0][k][1], rr = out[1][k][0], ri = out[1][k][1];
            energy[0][e][b] += lr * lr + li * li;
            energy[1][e][b] += rr * rr + ri * ri;
            cross[e][b] += lr * rr + li * ri;
            crossImag[e][b] += (k < 2 ? -1 : 1) * (li * rr - lr * ri);
        }
    }
}

/* Phase deltas wrap in eighths of a turn. */
static int ps_phase(float real, float imag)
{
    return (int)lrintf(atan2f(imag, real) * (4.0f / 3.14159265358979323846f)) & 7;
}

typedef struct {
    int8_t iid[PS_BANDS], icc[PS_BANDS];
    int8_t ipd[PS_PHASE_BANDS], opd[PS_PHASE_BANDS];
    int enable_icc, enable_phase;
} PsFrameParams;

static void ps_analyze_frame(float energy[2][64], const float crossReal[64],
                             const float crossImag[64], PsFrameParams *fd, int bands, int phases)
{
    float l = 0, r = 0, cross = 0;
    for (int k = 0; k < PS_BANDS; k++) {
        l += energy[0][k];
        r += energy[1][k];
        cross += crossReal[k];
    }
    float rho = cross / (sqrtf(l * r) + SBR_ENERGY_FLOOR);
    float w = 0.5f + 0.5f * fmaxf(0, fminf(1, -rho));
    fd->enable_phase = 0;

    for (int b = 0; b < bands; b++) {
        float eL = energy[0][b], eR = energy[1][b];
        float eLR = crossReal[b], eLRi = crossImag[b];

        /* Fine IID levels minimize the dB balance error. */
        float iid_db = 3.01029996f * log2f((eL + SBR_ENERGY_FLOOR) / (eR + SBR_ENERGY_FLOOR));
        int lvl = 0;
        while (lvl < 30 && iid_db >= 0.5f * (ps_iid_db_fine[lvl] + ps_iid_db_fine[lvl + 1]))
            lvl++;
        fd->iid[b] = lvl - 15;

        float denom = sqrtf(eL * eR) + SBR_ENERGY_FLOOR;
        float icc = eLR / denom;
        if (b < phases) {
            fd->ipd[b] = fd->opd[b] = 0;
            if (eL + eR >= 1e-6f * (l + r)) {
                float coherence = hypotf(eLR, eLRi) / denom;
                if (coherence >= 0.7f) {
                    int ipd = ps_phase(eLR, eLRi);
                    if (ipd != 0 && ipd != 4) {
                        fd->ipd[b] = ipd;
                        fd->opd[b] = ps_phase(w * eL + (1 - w) * eLR, (1 - w) * eLRi);
                        fd->enable_phase = 1;
                        icc = coherence;
                    }
                }
            }
        }
        icc = fmaxf(-1.0f, fminf(icc, 1.0f));
        int best_icc = 0;
        float best_err = 2.0f;
        for (int i = 0; i <= SBR_PS_ICC_MAX_INDEX; i++) {
            float err = fabsf(icc - ps_icc_invq[i]);
            if (err < best_err) {
                best_err = err;
                best_icc = i;
            }
        }
        fd->icc[b] = best_icc;
    }

    /* Omit ICC when every band matches the coherent decoder default. */
    fd->enable_icc = 0;
    for (int b = 0; b < bands; b++) {
        if (fd->icc[b] > 0) {
            fd->enable_icc = 1;
            break;
        }
    }
}

/* The last envelope of a frame is what the next frame's temporal deltas reference. */
static void ps_capture_ref(PsRef *cur, const SbrFrameData *fd)
{
    int e = fd->ps_num_env == 2;
    memcpy(cur->iid, e ? fd->ps_env2.iid : fd->iid, sizeof(cur->iid));
    if (fd->enable_icc)
        memcpy(cur->icc, e ? fd->ps_env2.icc : fd->icc, sizeof(cur->icc));
    else
        memset(cur->icc, 0, sizeof(cur->icc));
    if (fd->enable_phase) {
        memcpy(cur->ipd, e ? fd->ps_env2.ipd : fd->ipd, sizeof(cur->ipd));
        memcpy(cur->opd, e ? fd->ps_env2.opd : fd->opd, sizeof(cur->opd));
    } else {
        memset(cur->ipd, 0, sizeof(cur->ipd));
        memset(cur->opd, 0, sizeof(cur->opd));
    }
    cur->band_mode = fd->ps_band_mode;
    cur->iid_quant = fd->ps_iid_quant;
    cur->icc_on = fd->enable_icc;
    cur->ext_on = fd->ps_ext;
    cur->valid = 1;
}

/* What the decoder holds from the last header it saw, packed for comparison. */
static uint8_t ps_state_of(const PsRef *ref)
{
    return (uint8_t)(ref->iid_quant | ref->icc_on << 1 | ref->ext_on << 2 | ref->band_mode << 3);
}

/* The extension is dropped only after a frame that carried an explicit enable_phase = 0:
 * decoders latch that flag, so they must see it cleared before the extension disappears.
 * Holding longer only wastes bits (corpus sweep over 1-8 frames: one is best). */
#define PS_EXT_HOLD 1

/* A payload is emitted SBR_FRAME_FIFO - 1 ticks after it is analyzed, but the core
 * produces no access unit for the first LOOKAHEAD_DEPTH ticks, so frame k is written
 * as access unit k + 1 and that is the frameCount whose header cadence it sees. */
#define PS_WRITE_LAG (SBR_FRAME_FIFO - 1 - LOOKAHEAD_DEPTH)
static int ps_header_due(const SBRInfo *sbr)
{
    return (sbr->psAnalyzed + PS_WRITE_LAG) % SBR_HEADER_PERIOD == 0;
}

/* A temporal reference is unusable across headers and band-mode changes. */
static const PsRef *ps_usable_ref(const PsRef *ref, int header, const SbrFrameData *fd)
{
    return (ref->valid && !header && ref->band_mode == fd->ps_band_mode) ? ref : NULL;
}

/* Drain frames skip analysis but are still written and counted. A silent payload carries no
 * header, so it must restate the state the decoder holds -- same grid and band count, ICC and
 * extension as they stand -- with flat parameters and no phase, or the next frame's header-less
 * bits would be read under the wrong layout. */
void PsNoteSilence(SBRInfo *sbr, SbrFrameData *fd)
{
    const PsRef *held = &sbr->psPred;
    if (held->valid) {
        fd->ps_iid_quant = held->iid_quant;
        fd->ps_band_mode = held->band_mode;
        fd->enable_icc = held->icc_on;
        fd->ps_ext = held->ext_on;
    }
    fd->enable_phase = 0;
    fd->ps_send_header = 0;
    ps_capture_ref(&sbr->psPred, fd);
    sbr->psAnalyzed++;
}

void PsAnalyze(SBRInfo *sbr, struct SignalAnalysis *sa, SbrFrameData *fd)
{
    fd->ps_band_mode = sbr->psNumEnv == 1 ? 0 : 1;
    int bands = fd->ps_band_mode ? PS_BANDS : PS_LOW_BANDS;
    int phases = fd->ps_band_mode ? PS_PHASE_BANDS : PS_LOW_PHASE_BANDS;
    /* Save low-rate core bits with one envelope and ten parameter bands. */
    fd->ps_num_env = sbr->psNumEnv;
    float energy[2][64], crossReal[64], crossImag[64];
    PsFrameParams params = {0};
    fd->enable_icc = fd->enable_phase = 0;
    for (int e = 0; e < fd->ps_num_env; e++) {
        memset(energy, 0, sizeof(energy));
        memset(crossReal, 0, sizeof(crossReal));
        memset(crossImag, 0, sizeof(crossImag));
        for (int p = e * 4 / fd->ps_num_env; p < (e + 1) * 4 / fd->ps_num_env; p++)
            for (int k = 0; k < PS_BANDS; k++) {
                int b = fd->ps_band_mode ? k : k / 2;
                energy[0][b] += sa->psE[0][p][k];
                energy[1][b] += sa->psE[1][p][k];
                crossReal[b] += sa->psCross[p][k];
                crossImag[b] += sa->psCrossIm[p][k];
            }
        ps_analyze_frame(energy, crossReal, crossImag, &params, bands, phases);
        fd->enable_icc |= params.enable_icc;
        fd->enable_phase |= params.enable_phase;
        memcpy(e ? fd->ps_env2.iid : fd->iid, params.iid, sizeof(params.iid));
        memcpy(e ? fd->ps_env2.icc : fd->icc, params.icc, sizeof(params.icc));
        memcpy(e ? fd->ps_env2.ipd : fd->ipd, params.ipd, sizeof(params.ipd));
        memcpy(e ? fd->ps_env2.opd : fd->opd, params.opd, sizeof(params.opd));
    }

    /* Rate-distortion decision: evaluate coarse vs. fine IID quantization. */
    /* The first analyzed frame follows the silent priming payloads, not nothing. */
    if (sbr->psAnalyzed == 0 && !sbr->psPred.valid) {
        sbr->psPred.valid = 1;
        sbr->psPred.iid_quant = 1; /* the silent priming payload is fine-grid, no ICC, no extension */
    }
    const PsRef held = sbr->psPred;
    const PsRef *prev_ref = ps_usable_ref(&sbr->psPred, ps_header_due(sbr), fd);

    const PsRef *fine_ref = (prev_ref && prev_ref->iid_quant == 1) ? prev_ref : NULL;
    const PsRef *coarse_ref = (prev_ref && prev_ref->iid_quant == 0) ? prev_ref : NULL;

    fd->ps_pred_used = prev_ref != NULL;
    fd->ps_pred_state = ps_state_of(&held);
    /* Coarse can only take effect where a header is free or coarse is already held, so
     * everywhere else the rate-distortion comparison is skipped outright. */
    /* The first analyzed frame also restates the state, so the chain never depends on the
     * seed matching the priming payloads. */
    int mandatory = ps_header_due(sbr) || sbr->psAnalyzed == 0;
    int rd = mandatory || !held.iid_quant;
    int fine_bits = 0;
    int coarse_bits = 0;
    int within = 1; /* every band lands within 1 dB of its fine level */

    int8_t coarse_e1[PS_BANDS] = {0}, coarse_e2[PS_BANDS] = {0};
    for (int e = 0; rd && e < fd->ps_num_env; e++) {
        const int8_t *fine_iid = e ? fd->ps_env2.iid : fd->iid;
        int8_t *coarse_iid = e ? coarse_e2 : coarse_e1;
        for (int b = 0; b < bands; b++) {
            int fine_idx = fine_iid[b] + 15;
            if (fine_idx < 0) fine_idx = 0;
            if (fine_idx > 30) fine_idx = 30;
            coarse_iid[b] = ps_iid_coarse_of_fine[fine_idx];
            within &= (int)((ps_iid_coarse_exact >> fine_idx) & 1u);
        }
    }

    /* The bit counts only matter where the distortion bound holds. */
    for (int e = 0; rd && within && e < fd->ps_num_env; e++) {
        const int8_t *fine_iid = e ? fd->ps_env2.iid : fd->iid;
        const int8_t *coarse_iid = e ? coarse_e2 : coarse_e1;
        fine_bits += count_ps_param_bits(fine_iid, e ? fd->iid : (fine_ref ? fine_ref->iid : NULL),
                                        bands, 0, faac_ps_huff_iid_df_fine_packed,
                                        faac_ps_huff_iid_dt_fine_packed,
                                        PS_HUFF_IID_DF_FINE_NSYMS, PS_HUFF_IID_DF_FINE_OFFSET);

        coarse_bits += count_ps_param_bits(coarse_iid, e ? coarse_e1 : (coarse_ref ? coarse_ref->iid : NULL),
                                          bands, 0, faac_ps_huff_iid_df_coarse_packed,
                                          faac_ps_huff_iid_dt_coarse_packed,
                                          PS_HUFF_IID_DF_COARSE_NSYMS, PS_HUFF_IID_DF_COARSE_OFFSET);
    }

    int bit_savings = fine_bits - coarse_bits;
    /* A grid switch costs a header, which the saving cannot repay, so coarse is entered only
     * where the header is free (a random-access point) and left whenever it stops fitting. */
    int coarse = rd && within && bit_savings >= 4;
    fd->ps_iid_quant = !coarse;
    if (coarse) {
        memcpy(fd->iid, coarse_e1, sizeof(fd->iid));
        if (fd->ps_num_env == 2)
            memcpy(fd->ps_env2.iid, coarse_e2, sizeof(fd->ps_env2.iid));
    }

    /* ICC follows the frame outright (holding it on and sending zeros measured no better);
     * the extension lingers one frame so the decoder sees phase cleared first. */
    int icc = fd->enable_icc, want_ext = fd->enable_phase;
    int ext = want_ext || (held.ext_on && sbr->psExtIdle < PS_EXT_HOLD);
    sbr->psExtIdle = ext && !want_ext ? sbr->psExtIdle + 1 : 0;
    fd->ps_ext = ext;
    fd->ps_send_header = mandatory || fd->ps_iid_quant != held.iid_quant ||
                         fd->ps_band_mode != held.band_mode || icc != held.icc_on ||
                         ext != held.ext_on;
    ps_capture_ref(&sbr->psPred, fd);
    sbr->psAnalyzed++;
}

/* Avoid cancellation in the mono sum while preserving mean stereo energy. */
static void ps_stereo_energy(const float *left, const float *right, int n, double *el,
                             double *er, double *ec)
{
    double l = 0, r = 0, cross = 0;
    for (int i = 0; i < n; i++) {
        l += (double)left[i] * left[i];
        r += (double)right[i] * right[i];
        cross += (double)left[i] * right[i];
    }
    *el = l;
    *er = r;
    *ec = cross;
}

static float ps_downmix_gain(double l, double r, double cross, float *weight)
{
    double rho = cross / (sqrt(l * r) + 1e-20);
    float w = 0.5f + 0.5f * (float)fmax(0.0, -rho);
    double energy = w * w * l + (1 - w) * (1 - w) * r + 2 * w * (1 - w) * cross;
    *weight = w;
    return energy > 1e-20 ? (float)sqrt(0.5 * (l + r) / energy) : 1;
}

void PsDownmix(float *left, const float *right, int n)
{
    double l, r, cross;
    ps_stereo_energy(left, right, n, &l, &r, &cross);
    float w, gain = ps_downmix_gain(l, r, cross, &w);
    for (int i = 0; i < n; i++)
        left[i] = gain * (w * left[i] + (1 - w) * right[i]);
}

static bool ps_carrier_spectrum(const float *lf, const float *rf, float *work, bool packed)
{
    for (int k = 0; k < (packed ? 257 : 512); k++) {
        int mirror = (512 - k) & 511;
        float lr, li, rr, ri;
        if (packed) {
            lr = 0.5f * (lf[k] + lf[mirror]);
            li = 0.5f * (lf[k + 512] - lf[mirror + 512]);
            rr = 0.5f * (lf[k + 512] + lf[mirror + 512]);
            ri = 0.5f * (lf[mirror] - lf[k]);
        } else {
            lr = lf[k];
            li = lf[k + 512];
            rr = rf[k];
            ri = rf[k + 512];
        }
        float real = 0.5f * (lr + rr);
        float imag = 0.5f * (li + ri);
        float target = sqrtf(0.5f * (lr * lr + rr * rr + li * li + ri * ri));
        float magnitude = hypotf(real, imag);
        /* Cancelling channels amplify FFT rounding and can change the
         * phase fallback below. Recompute this hop with separate FFTs. */
        if (packed && magnitude < 1e-3f * target)
            return false;
        if (magnitude < 1e-5f * target) {
            real = lr;
            imag = li;
            magnitude = hypotf(real, imag);
        }
        float gain = magnitude > 1e-20f ? target / magnitude : 0;
        work[k] = real * gain;
        work[k + 512] = -imag * gain;
        if (packed && k > 0 && k < 256) {
            work[mirror] = work[k];
            work[mirror + 512] = -work[k + 512];
        }
    }
    return true;
}

void PsSpectralDownmix(PsCarrier *state, float *left, float *right, float *carrier, int n)
{
    float *work = state->work;
    float *lf = state->leftSpectrum;
    if (!state->initialized) {
        for (int i = 0; i < 512; i++)
            state->window[i] = 0.5f - 0.5f * cosf(2.0f * 3.14159265358979323846f * i / 512);
        for (int i = 0; i < 256; i++) {
            float w0 = state->window[i], w1 = state->window[i + 256];
            state->invWindowPower[i] = 1.0f / (w0 * w0 + w1 * w1);
        }
        state->initialized = 1;
    }
    for (int offset = 0; offset < n; offset += 256) {
        float *inputs[2] = {left + offset, right + offset};
        /* Pack two real channels into one complex transform. Conjugate
         * symmetry separates their spectra without a second forward FFT. */
        for (int ch = 0; ch < 2; ch++) {
            float *dst = work + ch * 512;
            for (int i = 0; i < 256; i++) {
                dst[i] = state->history[ch][i] * state->window[i];
                dst[i + 256] = inputs[ch][i] * state->window[i + 256];
                float sample = inputs[ch][i];
                inputs[ch][i] = state->history[ch][i];
                state->history[ch][i] = sample;
            }
        }
        fft(work, lf, FFT_LOGM_LONG);
        if (!ps_carrier_spectrum(lf, lf, work, true)) {
            float rf[1024] = {0};
            float *spectra[2] = {lf, rf};
            /* Inputs hold the previous hop and history the current one; rebuild
             * the windowed channels from them. */
            for (int ch = 0; ch < 2; ch++) {
                for (int i = 0; i < 256; i++) {
                    work[i] = inputs[ch][i] * state->window[i];
                    work[i + 256] = state->history[ch][i] * state->window[i + 256];
                }
                memset(work + 512, 0, 512 * sizeof(float));
                fft(work, spectra[ch], FFT_LOGM_LONG);
            }
            ps_carrier_spectrum(lf, rf, work, false);
        }
        fft(work, lf, FFT_LOGM_LONG);
        for (int i = 0; i < 256; i++) {
            float w0 = state->window[i], w1 = state->window[i + 256];
            carrier[offset + i] = (state->overlap[i] + lf[i] * (w0 / 512)) * state->invWindowPower[i];
            state->overlap[i] = lf[i + 256] * (w1 / 512);
        }
    }
}

static int ps_phase_body(const SbrFrameData *fd, BitStream *bs, const PsRef *ref)
{
    /* Two reserved zero bits, then the phase enable flag. */
    PutBit(bs, fd->enable_phase, 3);
    int n = 3;
    if (fd->enable_phase) {
        for (int e = 0; e < (fd->ps_num_env ? fd->ps_num_env : 1); e++) {
            const int8_t *params[2] = {e ? fd->ps_env2.ipd : fd->ipd,
                                       e ? fd->ps_env2.opd : fd->opd};
            const int8_t *prev[2] = {e     ? fd->ipd
                                  : ref ? ref->ipd
                                        : NULL,
                                  e     ? fd->opd
                                  : ref ? ref->opd
                                        : NULL};
            const PsHuffPackedEntry *df[2] = {faac_ps_huff_ipd_df_packed,
                                              faac_ps_huff_opd_df_packed};
            const PsHuffPackedEntry *dt[2] = {faac_ps_huff_ipd_dt_packed,
                                              faac_ps_huff_opd_dt_packed};
            for (int p = 0; p < 2; p++)
                n += write_ps_params(bs, params[p], prev[p],
                                     (fd->ps_band_mode ? PS_PHASE_BANDS : PS_LOW_PHASE_BANDS), 1,
                                     df[p], dt[p], 8, 0);
        }
    }
    /* The reserved bit must be zero, and the body pads to a byte boundary. */
    int tail = 1 + ((8 - ((n + 1) & 7)) & 7);
    PutBit(bs, 0, tail);
    return n + tail;
}

/* The byte count precedes the body, so it is sized by a counting pass first. */
static int ps_write_phase(const SbrFrameData *fd, BitStream *bs, const PsRef *ref)
{
    BitStream count;
    ps_counter(&count);
    int n = ps_phase_body(fd, &count, ref);
    int bytes = n / 8;
    int bits = (bytes >= 15 ? 12 : 4) + n;
    PutBit(bs, bytes >= 15 ? 15 : bytes, 4);
    if (bytes >= 15)
        PutBit(bs, bytes - 15, 8);
    ps_phase_body(fd, bs, ref);
    return bits;
}

/* Where the bits of one PS payload go; filled only when a caller asks (stats). */
typedef struct {
    int header, iid, icc, ext, pad;
} PsBitSplit;

static int ps_write(const SbrFrameData *fd, BitStream *bs, const PsRef *ref, PsBitSplit *split,
                    int force_header)
{
    /* Index 0 would mean no envelope, i.e. hold the previous image. */
    int n_env = fd->ps_num_env ? fd->ps_num_env : 1;
    int iid_quant = fd->ps_iid_quant != 0;
    int iid_mode = (iid_quant ? 3 : 0) + fd->ps_band_mode;
    /* Extension ID and header flag; the header restates the held state only when it changed. */
    int send_header = fd->ps_send_header || force_header;
    PutBit(bs, (SBR_EXT_ID_PS << 1) | send_header, 3);
    int n = 3;
    if (send_header) {
        /* IID enable/mode, ICC enable (and mode), then the extension flag. */
        PutBit(bs, (1 << 4) | (iid_mode << 1) | fd->enable_icc, 5);
        n += 5;
        if (fd->enable_icc) {
            PutBit(bs, fd->ps_band_mode, 3);
            n += 3;
        }
        PutBit(bs, fd->ps_ext, 1);
        n += 1;
    }
    /* Fixed frame class and the envelope count. */
    PutBit(bs, n_env == 2 ? 2 : 1, 3);
    n += 3;
    if (split) split->header = n;

    const PsHuffPackedEntry *iid_df = iid_quant ? faac_ps_huff_iid_df_fine_packed : faac_ps_huff_iid_df_coarse_packed;
    const PsHuffPackedEntry *iid_dt = iid_quant ? faac_ps_huff_iid_dt_fine_packed : faac_ps_huff_iid_dt_coarse_packed;
    int iid_nsyms = iid_quant ? PS_HUFF_IID_DF_FINE_NSYMS : PS_HUFF_IID_DF_COARSE_NSYMS;
    int iid_offset = iid_quant ? PS_HUFF_IID_DF_FINE_OFFSET : PS_HUFF_IID_DF_COARSE_OFFSET;

    const PsRef *iid_ref = (ref && ref->iid_quant == fd->ps_iid_quant) ? ref : NULL;

    for (int e = 0; e < n_env; e++)
        n += write_ps_params(bs, e ? fd->ps_env2.iid : fd->iid,
                             e     ? fd->iid
                             : iid_ref ? iid_ref->iid
                                       : NULL,
                             (fd->ps_band_mode ? PS_BANDS : PS_LOW_BANDS), 0,
                             iid_df, iid_dt,
                             iid_nsyms, iid_offset);
    if (split) split->iid = n - split->header;
    if (fd->enable_icc)
        for (int e = 0; e < n_env; e++)
            n += write_ps_params(bs, e ? fd->ps_env2.icc : fd->icc,
                                 e     ? fd->icc
                                 : ref ? ref->icc
                                       : NULL,
                                 (fd->ps_band_mode ? PS_BANDS : PS_LOW_BANDS), 0,
                                 faac_ps_huff_icc_df_packed, faac_ps_huff_icc_dt_packed,
                                 PS_HUFF_ICC_NSYMS, PS_HUFF_ICC_OFFSET);

    if (split) split->icc = n - split->header - split->iid;
    int ext = fd->ps_ext ? ps_write_phase(fd, bs, ref) : 0;
    if (split) split->ext = ext;
    n += ext;

    /* The extension size is byte-based, so pad the payload to a byte boundary. */
    int pad = (8 - (n & 7)) & 7;
    PutBit(bs, 0, pad);
    if (split) split->pad = pad;
    return n + pad;
}

int PsWrite(const SbrFrameData *fd, BitStream *bs, const PsRef *ref)
{
    return ps_write(fd, bs, ref, NULL, 0);
}

/* Configuration and carrier work run once per frame, outside the QMF kernels. */
void PsConfigure(SBRInfo *sbr, bool active, unsigned long bitrate, int rateLimited)
{
    sbr->is_he_v2 = active;
    sbr->psNumEnv = rateLimited && bitrate <= PS_LOW_RATE_MAX_BITRATE ? 1 : 2;
}

void PsPrepareCarrier(SBRContext *sCtx, float *fullPtrs[])
{
    float *spectralCarrier = sCtx->psSpectralCarrier;
    PsSpectralDownmix(&sCtx->psCarrier, fullPtrs[0], fullPtrs[1], spectralCarrier, 2 * FRAME_LEN);
    double l, r, cross;
    ps_stereo_energy(fullPtrs[0], fullPtrs[1], 2 * FRAME_LEN, &l, &r, &cross);
    sCtx->psCarrier.smoothL = 0.9 * sCtx->psCarrier.smoothL + l;
    sCtx->psCarrier.smoothR = 0.9 * sCtx->psCarrier.smoothR + r;
    sCtx->psCarrier.smoothCross = 0.9 * sCtx->psCarrier.smoothCross + cross;
    double rho = sCtx->psCarrier.smoothCross /
                 (sqrt(sCtx->psCarrier.smoothL * sCtx->psCarrier.smoothR) + 1e-20);
    float blend = (float)fmax(0, fmin(1, (0.99 - rho) / 0.005));
    /* The time-domain carrier only matters for nearly identical channels. */
    if (blend < 1) {
        float w, gain = ps_downmix_gain(l, r, cross, &w);
        for (int i = 0; i < 2 * FRAME_LEN; i++) {
            float original = gain * (w * fullPtrs[0][i] + (1 - w) * fullPtrs[1][i]);
            spectralCarrier[i] = blend * spectralCarrier[i] + (1 - blend) * original;
        }
    }
}

void PsCommitCarrier(SBRContext *sCtx, float *output)
{
    memcpy(output, sCtx->psSpectralCarrier, sizeof(sCtx->psSpectralCarrier));
}

/* Keep dry runs and retries on the preceding access unit's reference. */
void PsWriteExtension(SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs)
{
    /* Mirror the SBR envelope reference parity: dry runs and core retries
     * may overwrite the current slot but never the previous access unit. */
    const PsRef *ref = ps_usable_ref(&sbr->psRef[~sbr->frameCount & 1], sbr->sendHeaderThisFrame, fd);
    BitStream count;
    ps_counter(&count);
    int psBits = ps_write(fd, &count, ref, NULL, sbr->sendHeaderThisFrame);
    int psBytes = (psBits + 7) / 8;
    PutBit(bs, 1, 1); /* bs_extended_data */
    PutBit(bs, psBytes < 15 ? psBytes : 15, 4);
    if (psBytes >= 15) PutBit(bs, psBytes - 15, 8);
    ps_write(fd, bs, ref, NULL, sbr->sendHeaderThisFrame);
    ps_capture_ref(&sbr->psRef[sbr->frameCount & 1], fd);
}

#ifdef FAAC_STATS
void PsCountFrame(const SBRInfo *sbr, const SbrFrameData *fd, int id_aac)
{
    /* The PS core is a mono SCE, so the element type says nothing about PS here. */
    (void)id_aac;
    if (!PsIsActive(sbr)) return;
    const PsRef *ref = ps_usable_ref(&sbr->psRef[~sbr->frameCount & 1], sbr->sendHeaderThisFrame, fd);
    g_faacStats.psFrames++;
    /* Analysis chose its quantizer against the predecessor it assumed; the writer must agree. */
    const PsRef *raw = &sbr->psRef[~sbr->frameCount & 1];
    if ((ref != NULL) != fd->ps_pred_used ||
        (!sbr->sendHeaderThisFrame && raw->valid && ps_state_of(raw) != fd->ps_pred_state))
        g_faacStats.psRefMismatchFrames++;
    if (fd->ps_send_header || sbr->sendHeaderThisFrame) g_faacStats.psHeaderFrames++;
    BitStream count;
    ps_counter(&count);
    PsBitSplit split = {0};
    int payload = ps_write(fd, &count, ref, &split, sbr->sendHeaderThisFrame);
    g_faacStats.psPayloadBits += payload;
    g_faacStats.psHdrBits += split.header;
    g_faacStats.psIidBits += split.iid;
    g_faacStats.psIccBits += split.icc;
    g_faacStats.psExtBits += split.ext;
    g_faacStats.psPadBits += split.pad;
    /* bs_extended_data flag, 4-bit count, optional 8-bit escape: the SBR-side wrapper. */
    g_faacStats.psWrapBits += (payload / 8 >= 15) ? 13 : 5;
    if (fd->ps_band_mode == 0) g_faacStats.psLowBandFrames++;
    if (fd->ps_iid_quant == 0) g_faacStats.psCoarseIidFrames++;
    if (fd->ps_num_env == 1) g_faacStats.psOneEnvelopeFrames++;
    if (fd->enable_icc) g_faacStats.psIccFrames++;
    if (fd->enable_phase) g_faacStats.psPhaseFrames++;
}

void PsStatsPrint(void)
{
    if (g_faacStats.psRefMismatchFrames)
        fprintf(stderr, " PS Reference Skew  : %u of %u frames analyzed against a different predecessor than written\n",
                g_faacStats.psRefMismatchFrames, g_faacStats.psFrames);
    if (g_faacStats.psFrames > 0)
    {
        unsigned int psf = g_faacStats.psFrames;
        fprintf(stderr, " PS Allocation       : Payload = %.1f bits/frame | Low bands = %5.1f%% | Coarse IID = %5.1f%% | 1 env = %5.1f%% | ICC = %5.1f%% | Phase = %5.1f%%\n",
                (double)g_faacStats.psPayloadBits / psf,
                100.0 * g_faacStats.psLowBandFrames / psf,
                100.0 * g_faacStats.psCoarseIidFrames / psf,
                100.0 * g_faacStats.psOneEnvelopeFrames / psf,
                100.0 * g_faacStats.psIccFrames / psf,
                100.0 * g_faacStats.psPhaseFrames / psf);
        fprintf(stderr, " PS Bits/frame       : hdr %.1f | iid %.1f | icc %.1f | ext %.1f | pad %.1f | wrapper %.1f | PS headers %5.1f%%\n",
                (double)g_faacStats.psHdrBits / psf, (double)g_faacStats.psIidBits / psf,
                (double)g_faacStats.psIccBits / psf, (double)g_faacStats.psExtBits / psf,
                (double)g_faacStats.psPadBits / psf, (double)g_faacStats.psWrapBits / psf,
                100.0 * g_faacStats.psHeaderFrames / psf);
    }
}
#endif
