/*
 * 2x interleaved-PCM upsampler. See upsample.h for rationale.
 */
#include <stdlib.h>
#include <string.h>
#include "upsample.h"

#define UP_HISTORY 16   /* 16 past samples per channel for the filter */
#define UP_NUM_TAPS 16  /* even-indexed taps of 31-tap halfband */

/*
 * 31-tap linear-phase symmetric halfband, windowed-sinc + Hamming.
 * h[k] for even k only (odd k is zero except center h[15] = 1.0,
 * which is handled separately as a scaled delay-line tap).
 *
 * Coefficients pre-scaled by 2 to compensate for zero-stuffing
 * energy loss; DC gain = 2 (one tap of 1.0 plus 16 even taps that
 * sum to ~1.0).
 *
 * h_even[k] = h[2k]; symmetric h_even[k] = h_even[15-k].
 */
static const float h_even[UP_NUM_TAPS] = {
    -0.00340f,  0.00586f, -0.01344f,  0.02814f,
    -0.05348f,  0.09804f, -0.19356f,  0.63022f,
     0.63022f, -0.19356f,  0.09804f, -0.05348f,
     0.02814f, -0.01344f,  0.00586f, -0.00340f
};

struct upsample2x {
    int channels;
    /* per-channel ring of UP_HISTORY past samples; index 0 = oldest. */
    float **history;
};

upsample2x_t *upsample2x_create(int channels)
{
    upsample2x_t *u;
    int c;
    if (channels <= 0) return NULL;
    u = (upsample2x_t *)calloc(1, sizeof(*u));
    if (!u) return NULL;
    u->channels = channels;
    u->history = (float **)calloc((size_t)channels, sizeof(float *));
    if (!u->history) { free(u); return NULL; }
    for (c = 0; c < channels; c++) {
        u->history[c] = (float *)calloc(UP_HISTORY, sizeof(float));
        if (!u->history[c]) {
            for (--c; c >= 0; --c) free(u->history[c]);
            free(u->history);
            free(u);
            return NULL;
        }
    }
    return u;
}

void upsample2x_destroy(upsample2x_t *u)
{
    int c;
    if (!u) return;
    for (c = 0; c < u->channels; c++) free(u->history[c]);
    free(u->history);
    free(u);
}

void upsample2x_process(upsample2x_t *u,
                        const float *in, int in_samples,
                        float *out)
{
    int m, c, k;
    int ch = u->channels;
    for (m = 0; m < in_samples; m++) {
        for (c = 0; c < ch; c++) {
            float *hist = u->history[c];
            float x_new = in[m * ch + c];
            /* Shift history left by one: hist[0] discarded, hist[15] = x_new. */
            for (k = 0; k < UP_HISTORY - 1; k++) hist[k] = hist[k + 1];
            hist[UP_HISTORY - 1] = x_new;
            /* y[2m]   = sum_{k=0..15} h_even[k] * x[m-k]
             *         = sum_{k=0..15} h_even[k] * hist[15-k]
             * Filter output (interpolated position).
             */
            {
                float acc = 0.0f;
                for (k = 0; k < UP_NUM_TAPS; k++)
                    acc += h_even[k] * hist[UP_HISTORY - 1 - k];
                out[(2 * m) * ch + c] = acc;
            }
            /* y[2m+1] = 1.0 * x[m-7] = hist[8] (kept-input position,
             * scaled by the center tap of the doubled-gain filter).
             */
            out[(2 * m + 1) * ch + c] = hist[UP_HISTORY - 1 - 7];
        }
    }
}
