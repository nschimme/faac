/*
 * Fast FFT-based IMDCT and Windowing
 */

#include "faad_internal.h"
#include "fft.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static float kbd_window_2048[2048];
static float sine_window_2048[2048];
static float kbd_window_256[256];
static float sine_window_256[256];

static FFT_Tables fft_tbl;
static bool tables_init = false;

static void init_windows(void)
{
    if (tables_init) return;

    fft_initialize(&fft_tbl);

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

    sum = 0.0;
    for (int i = 0; i < 128; i++) {
        double v = (2.0 * i / 128.0) - 1.0;
        double term = cosh(alpha * sqrt(1.0 - v * v));
        sum += term;
    }
    run_sum = 0.0;
    for (int i = 0; i < 128; i++) {
        double v = (2.0 * i / 128.0) - 1.0;
        run_sum += cosh(alpha * sqrt(1.0 - v * v));
        kbd_window_256[i] = sqrt(run_sum / sum);
        kbd_window_256[255 - i] = kbd_window_256[i];
    }

    tables_init = true;
}

static void fast_imdct(const float *in, float *out, int n)
{
    int n2 = n / 2;
    int n4 = n / 4;
    int logm = 0;
    while ((1 << logm) < n2) logm++;

    float xr[1024], xi[1024];

    /* Pre-twiddle */
    for (int k = 0; k < n4; k++) {
        float re = in[2 * k];
        float im = in[n2 - 1 - 2 * k];
        float angle = (float)M_PI * (2 * k + 0.5f) / n;
        float c = cosf(angle);
        float s = sinf(angle);
        xr[k] = re * c + im * s;
        xi[k] = im * c - re * s;
    }

    fft(&fft_tbl, xr, xi, logm - 1);

    /* Post-twiddle and mirror */
    for (int k = 0; k < n4; k++) {
        float angle = (float)M_PI * (2 * k + 0.5f + n2) / (2 * n);
        float c = cosf(angle);
        float s = sinf(angle);
        float re = xr[k] * c - xi[k] * s;
        float im = xi[k] * c + xr[k] * s;

        out[2 * k] = -re;
        out[n2 - 1 - 2 * k] = im;
        out[n2 + 2 * k] = im;
        out[n - 1 - 2 * k] = re;
    }
}

void imdct_and_window(struct faad_decoder *dec, uint32_t ch, ICSInfo *ics, float *spec, float *out_pcm)
{
    init_windows();

    float imdct_out[FRAME_LEN_LONG * 2];
    memset(imdct_out, 0, sizeof(imdct_out));

    const float *win_long = (ics->window_shape == KBD_WINDOW) ? kbd_window_2048 : sine_window_2048;
    const float *win_short = (ics->window_shape == KBD_WINDOW) ? kbd_window_256 : sine_window_256;

    if (ics->window_sequence == EIGHT_SHORT_SEQUENCE) {
        float short_out[256];
        for (int w = 0; w < 8; w++) {
            fast_imdct(spec + w * 128, short_out, 256);
            for (int i = 0; i < 256; i++) {
                short_out[i] *= win_short[i];
            }
            int offset = 448 + w * 128;
            for (int i = 0; i < 256; i++) {
                imdct_out[offset + i] += short_out[i];
            }
        }
    } else {
        fast_imdct(spec, imdct_out, 2048);
        if (ics->window_sequence == ONLY_LONG_SEQUENCE) {
            for (int i = 0; i < 2048; i++) {
                imdct_out[i] *= win_long[i];
            }
        } else if (ics->window_sequence == LONG_START_SEQUENCE) {
            for (int i = 0; i < 1024; i++) {
                imdct_out[i] *= win_long[i];
            }
            /* 1024..1447: 1.0f */
            for (int i = 1448; i < 1704; i++) {
                imdct_out[i] *= win_short[i - 1448];
            }
            for (int i = 1704; i < 2048; i++) {
                imdct_out[i] = 0.0f;
            }
        } else if (ics->window_sequence == LONG_STOP_SEQUENCE) {
            for (int i = 0; i < 448; i++) {
                imdct_out[i] = 0.0f;
            }
            for (int i = 448; i < 704; i++) {
                imdct_out[i] *= win_short[i - 448];
            }
            /* 704..1023: 1.0f */
            for (int i = 1024; i < 2048; i++) {
                imdct_out[i] *= win_long[i];
            }
        }
    }

    /* Overlap-add with previous frame overlap buffer */
    for (int i = 0; i < FRAME_LEN_LONG; i++) {
        out_pcm[i] = imdct_out[i] + dec->overlap[ch][i];
        dec->overlap[ch][i] = imdct_out[FRAME_LEN_LONG + i];
    }
}
