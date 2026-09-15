/*
 * Temporal Noise Shaping (TNS) Decoder
 */

#include "faad_internal.h"
#include "sfb_tables.h"

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
        int max_sfb = ics->max_sfb;

        for (int f = 0; f < ics->tns_n_filt[w]; f++) {
            int order = ics->tns_order[w][f];
            if (order == 0) continue;

            int length = ics->tns_length[w][f];
            int dir = ics->tns_direction[w][f];

            /* Calculate start and stop spectral line index from SFB bounds */
            int start_sfb = max_sfb;
            int end_sfb = (max_sfb > length) ? (max_sfb - length) : 0;
            max_sfb = end_sfb;

            int start_line = ics->sfb_offsets[end_sfb];
            int end_line = ics->sfb_offsets[start_sfb];
            int num_lines = end_line - start_line;

            if (num_lines <= 0) continue;

            /* Convert quantized Reflection Coefficients (parcor) to LPC coefficients via Levinson-Durbin step-down */
            float rc[32];
            float lpc[32];
            for (int i = 0; i < order; i++) {
                int8_t val = ics->tns_coef[w][f][i];
                rc[i] = sinf((float)val * (float)M_PI / 16.0f); /* ISO 14496-3 parcor conversion */
            }

            for (int m = 0; m < order; m++) {
                lpc[m] = rc[m];
                for (int i = 0; i < m; i++) {
                    float tmp = lpc[i];
                    lpc[i] = tmp + rc[m] * lpc[m - 1 - i];
                }
            }

            tns_ar_filter(window_spec + start_line, num_lines, dir, lpc, order);
        }
    }
}
