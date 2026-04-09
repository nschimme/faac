#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "pseudo_sbr.h"

static float fast_rand(uint32_t *seed) {
    *seed = 1664525 * (*seed) + 1013904223;
    return (float)(*seed) / 4294967296.0f;
}

int PseudoSBRShouldEnable(unsigned long bitRatePerChannel, unsigned int sampleRate) {
    if (bitRatePerChannel > 0 && bitRatePerChannel <= 18000 && sampleRate <= 44100) return 1;
    return 0;
}

unsigned int PseudoSBRTargetBW(unsigned long bitRatePerChannel, unsigned int sampleRate) {
    if (bitRatePerChannel <= 8000) return 3500;
    if (bitRatePerChannel <= 16000) return 5000;
    return 6000;
}

void PseudoSBRApply(faac_real *freqBuff, int sbr_start_sfb, const int *cb_widths, unsigned long bitRatePerChannel) {
    int max_bin = 0, b;
    uint32_t seed = 0x12345678;
    for (b = 0; b < sbr_start_sfb; b++) max_bin += cb_widths[b];
    int sbr_start = max_bin;
    int sbr_end = FRAME_LEN;
    float gain = 0.125f; // -18dB

    if (sbr_start >= sbr_end) return;

    float expansion = 0.5f;

    int src_limit = (int)(sbr_start * expansion);
    if (src_limit < 1) src_limit = 1;

    for (int i = sbr_start; i < sbr_end; i++) {
        int src = sbr_start - (i - sbr_start + 1);
        while (src < 0) src += src_limit;

        freqBuff[i] = freqBuff[src] * gain;

        // Fast transition ramp (4 bins)
        if (i < sbr_start + 4) {
            freqBuff[i] *= (float)(i - sbr_start + 1) / 5.0f;
        }

        // Signal-proportional noise (minimal)
        float noise = (fast_rand(&seed) - 0.5f) * 0.05f * (float)fabs(freqBuff[src]);
        freqBuff[i] += noise;

        // Comfort noise floor (minimal)
        freqBuff[i] += (fast_rand(&seed) - 0.5f) * 0.001f;
    }
}
