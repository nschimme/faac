/*
 * FAAC - Freeware Advanced Audio Coder
 *
 * 2:1 FIR downsampler for HE-AAC core signal preparation.
 */

#include <stdlib.h>
#include <string.h>

#include "resample.h"
#include "coder.h"

static const faac_real fir_coeffs[RESAMPLE_FILTER_LEN] = {
     (faac_real) 6.7547e-05f,  (faac_real) 2.4177e-04f,
    (faac_real)-1.3024e-04f,  (faac_real)-5.6920e-04f,
     (faac_real) 1.6010e-04f,  (faac_real) 1.0970e-03f,
    (faac_real)-9.5138e-05f,  (faac_real)-1.8736e-03f,
    (faac_real)-1.5518e-04f,  (faac_real) 2.9367e-03f,
     (faac_real) 7.1261e-04f,  (faac_real)-4.3054e-03f,
    (faac_real)-1.7324e-03f,  (faac_real) 5.9735e-03f,
     (faac_real) 3.4076e-03f,  (faac_real)-7.9054e-03f,
    (faac_real)-5.9805e-03f,  (faac_real) 1.0034e-02f,
     (faac_real) 9.7720e-03f,  (faac_real)-1.2263e-02f,
    (faac_real)-1.5255e-02f,  (faac_real) 1.4473e-02f,
     (faac_real) 2.3243e-02f,  (faac_real)-1.6533e-02f,
    (faac_real)-3.5413e-02f,  (faac_real) 1.8307e-02f,
     (faac_real) 5.6117e-02f,  (faac_real)-1.9675e-02f,
    (faac_real)-1.0143e-01f,  (faac_real) 2.0538e-02f,
     (faac_real) 3.1672e-01f,  (faac_real) 4.7917e-01f,
     (faac_real) 3.1672e-01f,  (faac_real) 2.0538e-02f,
    (faac_real)-1.0143e-01f,  (faac_real)-1.9675e-02f,
     (faac_real) 5.6117e-02f,  (faac_real) 1.8307e-02f,
    (faac_real)-3.5413e-02f,  (faac_real)-1.6533e-02f,
     (faac_real) 2.3243e-02f,  (faac_real) 1.4473e-02f,
    (faac_real)-1.5255e-02f,  (faac_real)-1.2263e-02f,
     (faac_real) 9.7720e-03f,  (faac_real) 1.0034e-02f,
    (faac_real)-5.9805e-03f,  (faac_real)-7.9054e-03f,
     (faac_real) 3.4076e-03f,  (faac_real) 5.9735e-03f,
    (faac_real)-1.7324e-03f,  (faac_real)-4.3054e-03f,
     (faac_real) 7.1261e-04f,  (faac_real) 2.9367e-03f,
    (faac_real)-1.5518e-04f,  (faac_real)-1.8736e-03f,
    (faac_real)-9.5138e-05f,  (faac_real) 1.0970e-03f,
     (faac_real) 1.6010e-04f,  (faac_real)-5.6920e-04f,
    (faac_real)-1.3024e-04f,  (faac_real) 2.4177e-04f,
     (faac_real) 6.7547e-05f
};

Resampler *ResampleOpen(int channels)
{
    Resampler *r = (Resampler *)calloc(1, sizeof(Resampler));
    if (r) r->channels = channels;
    return r;
}

void ResampleClose(Resampler *r)
{
    free(r);
}

int Resample2to1(Resampler *r,
                 faac_real *input[MAX_CHANNELS],
                 int input_len,
                 faac_real *output[MAX_CHANNELS])
{
    int output_len = input_len / 2;
    const int H = RESAMPLE_FILTER_LEN - 1;            /* 62 */
    const int HALF = RESAMPLE_FILTER_LEN / 2;         /* 31 */
    int ch, i, j;

    for (ch = 0; ch < r->channels; ch++) {
        faac_real * __restrict in  = input[ch];
        faac_real * __restrict out = output[ch];
        faac_real * __restrict hist = r->buf[ch];

        /* Fixed-size buffer to avoid VLA (MSVC portability). */
        faac_real combined[2112];
        int actual_input = input_len > 2048 ? 2048 : input_len;

        memcpy(combined,     hist, H            * sizeof(faac_real));
        memcpy(combined + H, in,   actual_input * sizeof(faac_real));

        const faac_real * __restrict p = combined + H;
        for (i = 0; i < output_len; i++) {
            faac_real sum = p[-HALF] * fir_coeffs[HALF];
            for (j = 0; j < HALF; j++) {
                sum += (p[-j] + p[-(H - j)]) * fir_coeffs[j];
            }
            *out++ = sum;
            p += 2;
        }

        memcpy(hist, combined + actual_input, H * sizeof(faac_real));
    }

    return output_len;
}
