/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2001 Menno Bakker
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

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <math.h>
#include "psy_tables.h"

float FaacBarkFromHz(float hz) {
    return 26.81f * hz / (1960.0f + hz) - 0.53f;
}

/* ISO 226 ATH (Absolute Threshold of Hearing) resampled for 0-24kHz, 90 points */
static const float FaacAthTable[90] = {
    74.0f, 68.0f, 62.0f, 57.0f, 52.0f, 48.0f, 44.0f, 41.0f, 38.0f, 35.0f,
    33.0f, 31.0f, 29.0f, 27.0f, 25.0f, 23.0f, 22.0f, 21.0f, 19.0f, 18.0f,
    17.0f, 16.0f, 15.0f, 14.0f, 13.0f, 12.0f, 11.0f, 11.0f, 10.0f, 9.0f,
    9.0f, 8.0f, 8.0f, 7.0f, 7.0f, 6.0f, 6.0f, 5.0f, 5.0f, 4.0f,
    4.0f, 3.0f, 3.0f, 2.0f, 2.0f, 2.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    1.0f, 1.0f, 2.0f, 2.0f, 2.0f, 3.0f, 3.0f, 4.0f, 4.0f, 5.0f,
    6.0f, 7.0f, 8.0f, 9.0f, 10.0f, 11.0f, 12.0f, 14.0f, 15.0f, 17.0f,
    19.0f, 21.0f, 23.0f, 25.0f, 27.0f, 30.0f, 33.0f, 36.0f, 39.0f, 42.0f,
    46.0f, 50.0f, 54.0f, 58.0f, 62.0f, 67.0f, 72.0f, 77.0f, 82.0f, 87.0f
};

static void InitPsyBands(float *sfb_bark, float *sfb_ath, int num_cb, int *cb_width, unsigned long sampleRate, int fft_size) {
    int i;
    int offset = 0;
    for (i = 0; i < num_cb; i++) {
        float start_hz = (float)offset * (float)sampleRate / (float)fft_size;
        offset += cb_width[i];
        float end_hz = (float)offset * (float)sampleRate / (float)fft_size;
        float mid_hz = (start_hz + end_hz) * 0.5f;
        sfb_bark[i] = FaacBarkFromHz(mid_hz);

        float ath_pos = mid_hz * 89.0f / 24000.0f;
        int ath_idx = (int)ath_pos;
        if (ath_idx < 0) {
            sfb_ath[i] = FaacAthTable[0];
        } else if (ath_idx >= 89) {
            sfb_ath[i] = FaacAthTable[89];
        } else {
            float frac = ath_pos - (float)ath_idx;
            sfb_ath[i] = FaacAthTable[ath_idx] * (1.0f - frac) + FaacAthTable[ath_idx + 1] * frac;
        }
    }
}

void FaacInitPsyContext(FaacPsyContext *psy, int sr_idx,
                       int num_cb_long, int *cb_width_long,
                       int num_cb_short, int *cb_width_short) {
    static const unsigned long sr_table[12] = {
        96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000
    };
    unsigned long sampleRate = (sr_idx >= 0 && sr_idx < 12) ? sr_table[sr_idx] : 44100;

    InitPsyBands(psy->sfb_bark_long, psy->sfb_ath_long, num_cb_long, cb_width_long, sampleRate, 2048);
    InitPsyBands(psy->sfb_bark_short, psy->sfb_ath_short, num_cb_short, cb_width_short, sampleRate, 256);
}
