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

/* Verify PS payload sizing and carrier energy preservation. */

#undef NDEBUG
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "sbr_internal.h"
#include "bitstream.h"
#include "ps.h"
#include "fft.h"
#include "stats.h"

#ifdef FAAC_STATS
/* This unit links PS directly, without the encoder that owns these counters. */
faacEncStats g_faacStats;
#endif

static int write_checked(const SbrFrameData *fd, const PsRef *ref, int offset)
{
    unsigned char first[512] = {0}, second[512] = {0};
    BitStream counter;
    InitBitStream(&counter, NULL, 4096);
    int count = PsWrite(fd, &counter, ref);
    for (int pass = 0; pass < 2; pass++) {
        unsigned char *data = pass ? second : first;
        memset(data, 0xa5, sizeof(first));
        BitStream bs;
        InitBitStream(&bs, data, sizeof(first));
        PutBit(&bs, 0, offset);
        int written = PsWrite(fd, &bs, ref);
        assert(count == written);
        assert(bs.currentBit == (unsigned)(offset + written));
    }
    assert(memcmp(first, second, sizeof(first)) == 0);
    return count;
}

static void test_ps_write(void)
{
    SbrFrameData fd = {0};

    for (int band_mode = 0; band_mode <= 1; band_mode++) {
        fd.ps_band_mode = band_mode;
        for (int n_env = 1; n_env <= 2; n_env++) {
            fd.ps_num_env = n_env;
            for (int flags = 0; flags < 4; flags++) {
                fd.enable_icc = flags & 1;
                fd.enable_phase = (flags >> 1) & 1;
                for (int seed = 0; seed < 64; seed++) {
                    PsRef ref = {0};
                    ref.valid = 1;
                    ref.band_mode = band_mode;
                    for (int b = 0; b < PS_BANDS; b++) {
                        fd.iid[b] = ((seed + 7 * b) % 31) - 15;
                        fd.icc[b] = (seed + 3 * b) & 7;
                        fd.ps_env2.iid[b] = ((seed + 11 * b + 1) % 31) - 15;
                        fd.ps_env2.icc[b] = (seed + 5 * b + 3) & 7;
                        ref.iid[b] = ((seed + 13 * b + 9) % 31) - 15;
                        ref.icc[b] = (seed + 7 * b + 5) & 7;
                    }
                    for (int b = 0; b < PS_PHASE_BANDS; b++) {
                        fd.ipd[b] = (seed + b) & 7;
                        fd.opd[b] = (seed + 5 * b) & 7;
                        fd.ps_env2.ipd[b] = (seed + 3 * b + 4) & 7;
                        fd.ps_env2.opd[b] = (seed + 7 * b + 2) & 7;
                        ref.ipd[b] = (seed + 2 * b + 3) & 7;
                        ref.opd[b] = (seed + 6 * b + 1) & 7;
                    }
                    for (int offset = 0; offset < 8; offset++) {
                        write_checked(&fd, NULL, offset);
                        write_checked(&fd, &ref, offset);
                    }
                }
            }
        }
    }

    /* Alternating phase deltas force the nested extension's 15-byte size form. */
    fd.ps_band_mode = 1;
    fd.ps_num_env = 2;
    fd.enable_icc = 0;
    fd.enable_phase = 1;
    for (int b = 0; b < PS_BANDS; b++) {
        fd.iid[b] = 0;
        fd.ps_env2.iid[b] = 0;
    }
    for (int b = 0; b < PS_PHASE_BANDS; b++) {
        fd.ipd[b] = (b & 1) ? 0 : 4;
        fd.opd[b] = (b & 1) ? 4 : 0;
        fd.ps_env2.ipd[b] = (b & 1) ? 4 : 0;
        fd.ps_env2.opd[b] = (b & 1) ? 0 : 4;
    }
    BitStream counter;
    InitBitStream(&counter, NULL, 4096);
    int phase_bits = PsWrite(&fd, &counter, NULL);
    fd.enable_phase = 0;
    InitBitStream(&counter, NULL, 4096);
    int empty_phase_bits = PsWrite(&fd, &counter, NULL);
    assert(phase_bits - empty_phase_bits > 120);

    for (int offset = 0; offset < 8; offset++) {
        fd.enable_phase = 1;
        write_checked(&fd, NULL, offset);
    }

    PsRef ref = {0};
    ref.valid = 1;
    fd.ps_num_env = 1;
    fd.enable_phase = 1;
    for (int band_mode = 0; band_mode <= 1; band_mode++) {
        fd.ps_band_mode = ref.band_mode = band_mode;
        int phase_bands = band_mode ? PS_PHASE_BANDS : PS_PHASE_BANDS / 2;

        /* Temporal deltas should beat alternating frequency deltas in both modes. */
        fd.enable_icc = 1;
        for (int b = 0; b < PS_BANDS; b++) {
            fd.iid[b] = (b & 1) ? 15 : -15;
            fd.icc[b] = (b & 1) ? 7 : 0;
            ref.iid[b] = fd.iid[b];
            ref.icc[b] = fd.icc[b];
        }
        for (int b = 0; b < PS_PHASE_BANDS; b++) {
            fd.ipd[b] = (b & 1) ? 4 : 0;
            fd.opd[b] = (b & 1) ? 0 : 4;
            ref.ipd[b] = fd.ipd[b];
            ref.opd[b] = fd.opd[b];
        }
        int stable_with_ref = write_checked(&fd, &ref, 0);
        int stable_without_ref = write_checked(&fd, NULL, 0);
        assert(stable_with_ref < stable_without_ref);

        /* Check the wrapped 7 -> 0 phase transition. */
        fd.enable_icc = 0;
        for (int b = 0; b < PS_BANDS; b++)
            fd.iid[b] = ref.iid[b] = 0;
        for (int b = 0; b < PS_PHASE_BANDS; b++) {
            fd.ipd[b] = (b < phase_bands && (b & 1)) ? 4 : 0;
            fd.opd[b] = (b < phase_bands && (b & 1)) ? 0 : 4;
            ref.ipd[b] = fd.ipd[b];
            ref.opd[b] = fd.opd[b];
        }
        ref.ipd[0] = ref.opd[0] = 7;
        int wrapped = write_checked(&fd, &ref, 0);
        int wrapped_without_ref = write_checked(&fd, NULL, 0);
        assert(wrapped < wrapped_without_ref);
        for (int offset = 0; offset < 8; offset++)
            write_checked(&fd, &ref, offset);

        /* ICC payload appears only when enabled. */
        fd.enable_icc = 0;
        for (int b = 0; b < PS_BANDS; b++)
            fd.icc[b] = ref.icc[b] = 0;
        int icc_off = write_checked(&fd, &ref, 0);
        fd.enable_icc = 1;
        int icc_on = write_checked(&fd, &ref, 0);
        assert(icc_on > icc_off);
    }
}

/* Two-FFT, full-spectrum carrier as the numerical reference. */
static void reference_spectral_downmix(PsCarrier *state, float *left, float *right, float *carrier, int n)
{
    float *work = state->work;
    float *lf = state->leftSpectrum;
    float rf[1024] = {0};
    if (!state->initialized) {
        for (int i = 0; i < 512; i++)
            state->window[i] = 0.5f - 0.5f * cosf(2.0f * 3.14159265358979323846f * i / 512);
        state->initialized = 1;
    }
    for (int offset = 0; offset < n; offset += 256) {
        float *inputs[2] = {left + offset, right + offset};
        float *spectra[2] = {lf, rf};
        for (int ch = 0; ch < 2; ch++) {
            for (int i = 0; i < 256; i++) {
                work[i] = state->history[ch][i] * state->window[i];
                work[i + 256] = inputs[ch][i] * state->window[i + 256];
                float sample = inputs[ch][i];
                inputs[ch][i] = state->history[ch][i];
                state->history[ch][i] = sample;
            }
            memset(work + 512, 0, 512 * sizeof(float));
            fft(work, spectra[ch], FFT_LOGM_LONG);
        }
        for (int k = 0; k < 512; k++) {
            float real = 0.5f * (lf[k] + rf[k]);
            float imag = 0.5f * (lf[k + 512] + rf[k + 512]);
            float target = sqrtf(0.5f * (lf[k] * lf[k] + rf[k] * rf[k] + lf[k + 512] * lf[k + 512] +
                                         rf[k + 512] * rf[k + 512]));
            float magnitude = hypotf(real, imag);
            if (magnitude < 1e-5f * target) {
                real = lf[k];
                imag = lf[k + 512];
                magnitude = hypotf(real, imag);
            }
            float gain = magnitude > 1e-20f ? target / magnitude : 0;
            work[k] = real * gain;
            work[k + 512] = -imag * gain;
        }
        fft(work, lf, FFT_LOGM_LONG);
        for (int i = 0; i < 256; i++) {
            float w0 = state->window[i], w1 = state->window[i + 256];
            carrier[offset + i] = (state->overlap[i] + lf[i] * (w0 / 512)) / (w0 * w0 + w1 * w1);
            state->overlap[i] = lf[i + 256] * (w1 / 512);
        }
    }
}

static void test_spectral_reference(void)
{
    enum { N = 2048 };
    unsigned seed = 1;
    for (int mode = 0; mode < 14; mode++) {
        PsCarrier actual = {0}, reference = {0};
        double error = 0, energy = 0, actual_energy = 0;
        for (int frame = 0; frame < 4; frame++) {
            float left[N], right[N], ref_left[N], ref_right[N], out[N], expected[N];
            for (int i = 0; i < N; i++) {
                seed = 1664525u * seed + 1013904223u;
                float l = (float)(seed >> 8) / 8388608.0f - 1;
                seed = 1664525u * seed + 1013904223u;
                float r = (float)(seed >> 8) / 8388608.0f - 1;
                switch (mode) {
                case 0: l = r = 0; break;
                case 1: r = l; break;
                case 2: r = -l; break;
                case 3: r = -l + 1e-4f * r; break;
                case 4: r = 0; break;
                case 5: l = 0; break;
                case 6: l = 0.3f; r = -0.1f; break;
                case 7: l = (i & 1) ? -0.3f : 0.3f; r = 0.1f; break;
                case 8: l *= 1e-12f; r *= 1e-12f; break;
                case 10: r = -l + 1e-5f * r; break;
                case 11: r = -l + 1e-3f * r; break;
                case 12: if ((i / 256 + frame) & 1) r = -l; break;
                case 13: if ((i / 256 + frame) & 1) l = r = 0; break;
                }
                left[i] = ref_left[i] = l;
                right[i] = ref_right[i] = r;
            }
            PsSpectralDownmix(&actual, left, right, out, N);
            reference_spectral_downmix(&reference, ref_left, ref_right, expected, N);
            assert(memcmp(left, ref_left, sizeof(left)) == 0);
            assert(memcmp(right, ref_right, sizeof(right)) == 0);
            for (int i = 0; i < N; i++) {
                assert(isfinite(out[i]));
                double d = (double)out[i] - expected[i];
                error += d * d;
                energy += (double)expected[i] * expected[i];
                actual_energy += (double)out[i] * out[i];
            }
        }
        /* The cancellation fallback keeps even near-antiphase inputs close
         * to the original carrier; ordinary FFT rounding is still allowed. */
        if (mode == 3 || mode == 10 || mode == 11) {
            /* A tiny nonzero mono residual is ill-conditioned relative to
             * the separate-transform reference, but mean energy stays close. */
            assert(error <= 1e-3 * energy + 1e-30);
            assert(fabs(actual_energy - energy) <= 1e-3 * energy + 1e-30);
        } else {
            assert(error <= 1e-10 * energy + 1e-30);
            assert(fabs(actual_energy - energy) <= 1e-5 * energy + 1e-30);
        }
    }
}

static void test_spectral_carrier(void)
{
    enum { SAMPLES = 4096, START = 1024, STOP = SAMPLES - 512 };
    for (int mode = 0; mode < 5; mode++) {
        PsCarrier state = {0};
        float left[SAMPLES], right[SAMPLES], input_left[SAMPLES], input_right[SAMPLES];
        float source[SAMPLES], carrier[SAMPLES];
        double source_energy = 0.0, carrier_energy = 0.0;

        for (int i = 0; i < SAMPLES; i++) {
            float x = 0.4f * sinf((float)i * 0.031f) + 0.2f * cosf((float)i * 0.073f);
            float l = mode == 0 || mode == 3 ? 0.0f : x;
            float r = mode == 0 || mode == 2 ? 0.0f : mode == 4 ? -x : x;
            left[i] = l;
            right[i] = r;
            input_left[i] = l;
            input_right[i] = r;
            source[i] = x;
        }

        PsSpectralDownmix(&state, left, right, carrier, SAMPLES);
        for (int i = 0; i < SAMPLES; i++)
            assert(isfinite(carrier[i]));

        if (mode == 0) {
            for (int i = 0; i < SAMPLES; i++)
                assert(fabsf(carrier[i]) < 1e-6f);
            continue;
        }

        for (int i = START; i < STOP; i++) {
            source_energy += 0.5 * ((double)input_left[i - 256] * input_left[i - 256] +
                                    (double)input_right[i - 256] * input_right[i - 256]);
            carrier_energy += (double)carrier[i] * carrier[i];
            if (mode == 1)
                assert(fabsf(carrier[i] - source[i - 256]) < 1e-4f);
        }
        assert(fabs(carrier_energy - source_energy) < 0.02 * source_energy);
    }
}

/* The blend must track the reference through the narrow correlation interval. */
static void test_carrier_blend(void)
{
    enum { N = 2 * FRAME_LEN };
    SBRContext actual = {0};
    PsCarrier reference = {0};
    unsigned seed = 42;
    int pure_time = 0, pure_spectral = 0, mixed = 0;
    for (int frame = 0; frame < 80; frame++) {
        float left[N] = {0}, right[N] = {0}, ref_left[N], ref_right[N], expected[N], original[N];
        static const float noise[] = {0, 0.1f, 0.16f, 0.2f, 1, 0, 0, 0};
        int mode = frame / 10;
        for (int i = 0; i < N; i++) {
            seed = 1664525u * seed + 1013904223u;
            float l = (float)(seed >> 8) / 16777216.0f - 0.5f;
            seed = 1664525u * seed + 1013904223u;
            float r = l + noise[mode] * ((float)(seed >> 8) / 16777216.0f - 0.5f);
            if (mode == 5) r = -l;
            if (mode == 6) r = 0;
            if (mode == 7) l = r = 0;
            left[i] = ref_left[i] = l;
            right[i] = ref_right[i] = r;
        }
        float *inputs[2] = {left, right};
        PsPrepareCarrier(&actual, inputs);
        PsSpectralDownmix(&reference, ref_left, ref_right, expected, N);
        assert(memcmp(left, ref_left, sizeof(left)) == 0);
        assert(memcmp(right, ref_right, sizeof(right)) == 0);
        memcpy(original, ref_left, sizeof(original));
        PsDownmix(original, ref_right, N);
        double l = 0, r = 0, cross = 0;
        for (int i = 0; i < N; i++) {
            l += (double)ref_left[i] * ref_left[i];
            r += (double)ref_right[i] * ref_right[i];
            cross += (double)ref_left[i] * ref_right[i];
        }
        reference.smoothL = 0.9 * reference.smoothL + l;
        reference.smoothR = 0.9 * reference.smoothR + r;
        reference.smoothCross = 0.9 * reference.smoothCross + cross;
        double rho = reference.smoothCross / (sqrt(reference.smoothL * reference.smoothR) + 1e-20);
        float blend = (float)fmax(0, fmin(1, (0.99 - rho) / 0.005));
        pure_time += blend == 0;
        pure_spectral += blend == 1;
        mixed += blend > 0 && blend < 1;
        for (int i = 0; i < N; i++) {
            expected[i] = blend * expected[i] + (1 - blend) * original[i];
            assert(fabsf(actual.psSpectralCarrier[i] - expected[i]) <= 1e-5f * (1.0f + fabsf(expected[i])));
        }
    }
    assert(pure_time && pure_spectral && mixed);
}

/* Counting passes and CBR retries must keep the previous access unit intact. */
static void test_extension_retry(void)
{
    SBRInfo sbr = {0};
    SbrFrameData fd = {0};
    fd.ps_num_env = 2;
    fd.enable_icc = 1;
    fd.enable_phase = 1;
    for (int band_mode = 0; band_mode <= 1; band_mode++) {
        fd.ps_band_mode = band_mode;
        for (int b = 0; b < PS_BANDS; b++) {
            fd.iid[b] = b % 7;
            fd.ps_env2.iid[b] = (b + 1) % 7;
        }
        for (int parity = 0; parity < 2; parity++) {
            sbr.frameCount = parity;
            sbr.is_he_v2 = 1;
            PsRef *previous = &sbr.psRef[~parity & 1];
            memset(previous, 0, sizeof(*previous));
            previous->valid = 1;
            previous->band_mode = band_mode;
            PsRef saved = *previous;
            for (int header = 0; header <= 1; header++) {
                sbr.sendHeaderThisFrame = header;
                unsigned char first[512], retry[512];
                BitStream dry, bs;
                InitBitStream(&dry, NULL, sizeof(first));
                PsWriteExtension(&sbr, &fd, &dry);
                InitBitStream(&bs, first, sizeof(first));
                PsWriteExtension(&sbr, &fd, &bs);
                assert(bs.currentBit == dry.currentBit);
                InitBitStream(&bs, retry, sizeof(retry));
                PsWriteExtension(&sbr, &fd, &bs);
                assert(bs.currentBit == dry.currentBit);
                assert(memcmp(first, retry, sizeof(first)) == 0);
                assert(memcmp(previous, &saved, sizeof(saved)) == 0);
            }
        }
    }
}

int main(void)
{
    fft_init();
    test_ps_write();
    test_extension_retry();
    test_spectral_carrier();
    test_spectral_reference();
    test_carrier_blend();

    for (int mode = 0; mode < 5; mode++) {
        float left[256], right[256];
        double source = 0;
        for (int i = 0; i < 256; i++) {
            float x = sinf((float)i * 0.17f);
            left[i] = mode == 2 || mode == 4 ? 0 : x;
            right[i] = mode == 1 || mode == 4 ? 0 : mode == 3 ? -x : x;
            source += 0.5 * ((double)left[i] * left[i] + (double)right[i] * right[i]);
        }
        PsDownmix(left, right, 256);
        double carrier = 0;
        for (int i = 0; i < 256; i++) {
            assert(isfinite(left[i]));
            carrier += (double)left[i] * left[i];
        }
        assert(fabs(carrier - source) < 1e-5 * (source + 1));
    }
    puts("PS payload and carrier: ok");
    return 0;
}
