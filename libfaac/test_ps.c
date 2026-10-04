/* PS payload sizing and energy-preserving carrier controls. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "sbr_internal.h"
#include "bitstream.h"
#include "ps.h"

int main(void)
{
    SbrFrameData fd = {0};
    unsigned char data[512];
    for (int flags = 0; flags < 4; flags++) {
        fd.enable_icc = flags & 1;
        fd.enable_phase = (flags >> 1) & 1;
        for (int seed = 0; seed < 64; seed++) {
            for (int b = 0; b < PS_BANDS; b++) {
                fd.iid[b] = ((seed + 7 * b) % 15) - 7;
                fd.icc[b] = (seed + 3 * b) & 7;
            }
            for (int b = 0; b < PS_PHASE_BANDS; b++) {
                fd.ipd[b] = (seed + b) & 7;
                fd.opd[b] = (seed + 5 * b) & 7;
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
