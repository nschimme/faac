/* Deterministic 16-bit PCM for the encoder tests: two tones, seeded noise bursts and
 * full-scale clicks in near-silence, so block switching and rate control both engage. */
#ifndef TEST_FAAC_PCM_H
#define TEST_FAAC_PCM_H

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include "../tests/ftest.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int16_t *pcm_make(uint32_t rate, uint32_t channels, uint32_t frames)
{
    int16_t *pcm = (int16_t *)malloc((size_t)frames * channels * sizeof(*pcm));
    uint32_t rng = 0x2545F491u;
    FT_REQUIRE(pcm);
    for (uint32_t i = 0; i < frames; i++) {
        double t = (double)i / rate;
        double v = 3000.0 * sin(2 * M_PI * 440.0 * t) + 1500.0 * sin(2 * M_PI * 3100.0 * t);
        if ((i / (rate / 4)) % 3 == 1)
            v += (double)(int32_t)(ft_rand(&rng) % 8001) - 4000.0;
        if (i % (rate / 3) < 48)
            v = (i & 1) ? 30000.0 : -30000.0;
        for (uint32_t c = 0; c < channels; c++)
            pcm[(size_t)i * channels + c] = (int16_t)(v * (c ? 0.7 : 1.0));
    }
    return pcm;
}

#endif
