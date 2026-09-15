/*
 * Temporal Noise Shaping (TNS) Decoder
 */

#include "faad_internal.h"

static void tns_ar_filter(float *spec, int length, int dir, const float *lpc, int order)
{
    int start = 0;
    int stop = length;
    int inc = 1;
    if (dir) {
        start = length - 1;
        stop = -1;
        inc = -1;
    }

    for (int i = start; i != stop; i += inc) {
        float sum = spec[i];
        for (int j = 1; j <= order; j++) {
            int idx = i - j * inc;
            if (idx >= 0 && idx < length) {
                sum -= lpc[j - 1] * spec[idx];
            }
        }
        spec[i] = sum;
    }
}

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

            /* Convert quantized Reflection Coefficients (parcor) to LPC coefficients via Levinson-Durbin step-down */
            float rc[32];
            float lpc[32];
            for (int i = 0; i < order; i++) {
                rc[i] = (float)ics->tns_coef[w][f][i] / 8.0f;
            }

            for (int m = 0; m < order; m++) {
                lpc[m] = rc[m];
                for (int i = 0; i < m; i++) {
                    float tmp = lpc[i];
                    lpc[i] = tmp + rc[m] * lpc[m - 1 - i];
                }
            }

            int num_lines = length * 8; /* length in SFBs convert to spectral lines approx */
            if (num_lines > 128) num_lines = 128;

            tns_ar_filter(window_spec, num_lines, dir, lpc, order);
        }
    }
}
