/* PS payload sizing and energy-preserving carrier controls. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "sbr_internal.h"
#include "bitstream.h"
#include "ps.h"
#include "fft.h"

static void test_ps_write(void)
{
    SbrFrameData fd = {0};
    unsigned char data[512];

    for (int n_env = 1; n_env <= 2; n_env++) {
        fd.ps_num_env = n_env;
        for (int flags = 0; flags < 4; flags++) {
            fd.enable_icc = flags & 1;
            fd.enable_phase = (flags >> 1) & 1;
            for (int seed = 0; seed < 64; seed++) {
                for (int b = 0; b < PS_BANDS; b++) {
                    fd.iid[b] = ((seed + 7 * b) % 31) - 15;
                    fd.icc[b] = (seed + 3 * b) & 7;
                    fd.ps_extra[0].iid[b] = ((seed + 11 * b + 1) % 31) - 15;
                    fd.ps_extra[0].icc[b] = (seed + 5 * b + 3) & 7;
                }
                for (int b = 0; b < PS_PHASE_BANDS; b++) {
                    fd.ipd[b] = (seed + b) & 7;
                    fd.opd[b] = (seed + 5 * b) & 7;
                    fd.ps_extra[0].ipd[b] = (seed + 3 * b + 4) & 7;
                    fd.ps_extra[0].opd[b] = (seed + 7 * b + 2) & 7;
                }
                for (int offset = 0; offset < 8; offset++) {
                    BitStream bs;
                    InitBitStream(&bs, data, sizeof(data));
                    PutBit(&bs, 0, offset);
                    int count = PsWrite(&fd, NULL, false);
                    int written = PsWrite(&fd, &bs, true);
                    assert(count == written);
                    assert(bs.currentBit == (unsigned)(offset + written));
                }
            }
        }
    }

    /* Make phase deltas large enough that their nested extension needs the
     * 15-byte escape size field. The disabled-phase stream is the same payload
     * prefix and an eight-bit empty phase extension. */
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
    int phase_bits = PsWrite(&fd, NULL, false);
    fd.enable_phase = 0;
    int empty_phase_bits = PsWrite(&fd, NULL, false);
    assert(phase_bits - empty_phase_bits > 120);

    for (int offset = 0; offset < 8; offset++) {
        fd.enable_phase = 1;
        BitStream bs;
        InitBitStream(&bs, data, sizeof(data));
        PutBit(&bs, 0, offset);
        int count = PsWrite(&fd, NULL, false);
        int written = PsWrite(&fd, &bs, true);
        assert(count == written);
        assert(bs.currentBit == (unsigned)(offset + written));
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
