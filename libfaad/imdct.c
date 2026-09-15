/*
 * IMDCT and Windowing
 */

#include "faad_internal.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static float kbd_window_2048[2048];
static float sine_window_2048[2048];
static float kbd_window_256[256];
static float sine_window_256[256];
static bool tables_init = false;

static void init_windows(void)
{
    if (tables_init) return;

    for (int i = 0; i < 2048; i++) {
        sine_window_2048[i] = sinf((float)M_PI * (i + 0.5f) / 2048.0f);
    }
    for (int i = 0; i < 256; i++) {
        sine_window_256[i] = sinf((float)M_PI * (i + 0.5f) / 256.0f);
    }

    double sum = 0.0;
    double alpha = 4.0;
    for (int i = 0; i < 1024; i++) {
        double v = (2.0 * i / 1024.0) - 1.0;
        double term = cosh(alpha * sqrt(1.0 - v * v));
        sum += term;
    }
    double run_sum = 0.0;
    for (int i = 0; i < 1024; i++) {
        double v = (2.0 * i / 1024.0) - 1.0;
        run_sum += cosh(alpha * sqrt(1.0 - v * v));
        kbd_window_2048[i] = sqrt(run_sum / sum);
        kbd_window_2048[2047 - i] = kbd_window_2048[i];
    }

    tables_init = true;
}

static void imdct_transform(const float *in, float *out, int n)
{
    int n2 = n / 2;
    for (int i = 0; i < n; i++) {
        double sum = 0.0;
        for (int k = 0; k < n2; k++) {
            sum += in[k] * cos((M_PI / n2) * (i + 0.5 + n2 * 0.5) * (k + 0.5));
        }
        out[i] = (float)(sum * 2.0 / n2);
    }
}

void imdct_and_window(struct faad_decoder *dec, uint32_t ch, ICSInfo *ics, float *spec, float *out_pcm)
{
    init_windows();

    float imdct_out[FRAME_LEN_LONG * 2];
    memset(imdct_out, 0, sizeof(imdct_out));

    if (ics->window_sequence == EIGHT_SHORT_SEQUENCE) {
        float short_out[256];
        for (int w = 0; w < 8; w++) {
            imdct_transform(spec + w * 128, short_out, 256);
            const float *win = (ics->window_shape == KBD_WINDOW) ? kbd_window_256 : sine_window_256;
            for (int i = 0; i < 256; i++) {
                short_out[i] *= win[i];
            }
            int offset = 448 + w * 128;
            for (int i = 0; i < 256; i++) {
                imdct_out[offset + i] += short_out[i];
            }
        }
    } else {
        imdct_transform(spec, imdct_out, 2048);
        const float *win = (ics->window_shape == KBD_WINDOW) ? kbd_window_2048 : sine_window_2048;
        for (int i = 0; i < 2048; i++) {
            imdct_out[i] *= win[i];
        }
    }

    /* Overlap-add with previous frame overlap buffer */
    for (int i = 0; i < FRAME_LEN_LONG; i++) {
        out_pcm[i] = imdct_out[i] + dec->overlap[ch][i];
        dec->overlap[ch][i] = imdct_out[FRAME_LEN_LONG + i];
    }
}
