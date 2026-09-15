/*
 * Temporal Noise Shaping (TNS) Decoder
 */

#include "faad_internal.h"

void apply_tns(ICSInfo *ics, float *spec)
{
    if (!ics->tns_data_present) return;

    for (int w = 0; w < ics->num_windows; w++) {
        float *window_spec = spec + w * 128;
        for (int f = 0; f < ics->tns_n_filt[w]; f++) {
            int order = ics->tns_order[w][f];
            if (order == 0) continue;

            int length = ics->tns_length[w][f];
            int dir = ics->tns_direction[w][f];

            /* Convert quantized Reflection Coefficients (LPC) to FIR filter */
            float lpc[32];
            for (int i = 0; i < order; i++) {
                lpc[i] = (float)ics->tns_coef[w][f][i] / 8.0f;
            }

            int start = 0;
            int stop = length;
            int inc = 1;
            if (dir) {
                start = length - 1;
                stop = -1;
                inc = -1;
            }

            /* All-pole / All-pass TNS synthesis filter */
            for (int i = start; i != stop; i += inc) {
                float sum = window_spec[i];
                for (int j = 1; j <= order; j++) {
                    int idx = i - j * inc;
                    if (idx >= 0 && idx < length) {
                        sum -= lpc[j - 1] * window_spec[idx];
                    }
                }
                window_spec[i] = sum;
            }
        }
    }
}
