/* PS payload sizing and energy-preserving carrier controls. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "sbr_internal.h"
#include "bitstream.h"
#include "ps.h"
#include "fft.h"

static int write_checked(const SbrFrameData *fd, const PsRef *ref, int offset)
{
    unsigned char first[512], second[512];
    int count = PsWrite(fd, NULL, false, ref);
    for (int pass = 0; pass < 2; pass++) {
        unsigned char *data = pass ? second : first;
        memset(data, 0xa5, sizeof(first));
        BitStream bs;
        InitBitStream(&bs, data, sizeof(first));
        PutBit(&bs, 0, offset);
        int written = PsWrite(fd, &bs, true, ref);
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
                        fd.ps_extra[0].iid[b] = ((seed + 11 * b + 1) % 31) - 15;
                        fd.ps_extra[0].icc[b] = (seed + 5 * b + 3) & 7;
                        ref.iid[b] = ((seed + 13 * b + 9) % 31) - 15;
                        ref.icc[b] = (seed + 7 * b + 5) & 7;
                    }
                    for (int b = 0; b < PS_PHASE_BANDS; b++) {
                        fd.ipd[b] = (seed + b) & 7;
                        fd.opd[b] = (seed + 5 * b) & 7;
                        fd.ps_extra[0].ipd[b] = (seed + 3 * b + 4) & 7;
                        fd.ps_extra[0].opd[b] = (seed + 7 * b + 2) & 7;
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

    /* Make phase deltas large enough that their nested extension needs the
     * 15-byte escape size field. The disabled-phase stream is the same payload
     * prefix and an eight-bit empty phase extension. */
    fd.ps_band_mode = 1;
    fd.ps_num_env = 2;
    fd.enable_icc = 0;
    fd.enable_phase = 1;
    for (int b = 0; b < PS_BANDS; b++) {
        fd.iid[b] = 0;
        fd.ps_extra[0].iid[b] = 0;
    }
    for (int b = 0; b < PS_PHASE_BANDS; b++) {
        fd.ipd[b] = (b & 1) ? 0 : 4;
        fd.opd[b] = (b & 1) ? 4 : 0;
        fd.ps_extra[0].ipd[b] = (b & 1) ? 4 : 0;
        fd.ps_extra[0].opd[b] = (b & 1) ? 0 : 4;
    }
    int phase_bits = PsWrite(&fd, NULL, false, NULL);
    fd.enable_phase = 0;
    int empty_phase_bits = PsWrite(&fd, NULL, false, NULL);
    assert(phase_bits - empty_phase_bits > 120);

    for (int offset = 0; offset < 8; offset++) {
        fd.enable_phase = 1;
        write_checked(&fd, NULL, offset);
    }

    /* A stable previous image makes temporal coding cheaper than the large
     * alternating frequency deltas. Check the written payload, not a copy of
     * the encoder's coding decision. */
    PsRef ref = {0};
    ref.valid = 1;
    fd.ps_num_env = 1;
    fd.enable_phase = 1;
    for (int band_mode = 0; band_mode <= 1; band_mode++) {
        fd.ps_band_mode = ref.band_mode = band_mode;
        int phase_bands = band_mode ? PS_PHASE_BANDS : PS_PHASE_BANDS / 2;

        /* A stable previous image makes temporal coding cheaper than the
         * large alternating frequency deltas in both PS band modes. */
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
        int stable_without_ref = PsWrite(&fd, NULL, false, NULL);
        assert(stable_with_ref < stable_without_ref);

        /* Exercise the wrapped 7 -> 0 phase transition while the other bands
         * have stable references. */
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
        int wrapped_without_ref = PsWrite(&fd, NULL, false, NULL);
        assert(wrapped < wrapped_without_ref);
        for (int offset = 0; offset < 8; offset++)
            write_checked(&fd, &ref, offset);

        /* Toggling ICC on against an all-zero old ICC image must add its
         * payload; toggling it off must omit that payload. */
        fd.enable_icc = 0;
        for (int b = 0; b < PS_BANDS; b++)
            fd.icc[b] = ref.icc[b] = 0;
        int icc_off = write_checked(&fd, &ref, 0);
        fd.enable_icc = 1;
        int icc_on = write_checked(&fd, &ref, 0);
        assert(icc_on > icc_off);
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

int main(void)
{
    fft_init();
    test_ps_write();
    test_spectral_carrier();

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
        for (int i = 0; i < 256; i++) { assert(isfinite(left[i])); carrier += (double)left[i] * left[i]; }
        assert(fabs(carrier - source) < 1e-5 * (source + 1));
    }
    puts("PS payload and carrier: ok");
    return 0;
}
