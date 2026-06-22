/*
 * FAAC - Freeware Advanced Audio Coder
 *
 * 2:1 FIR downsampler for HE-AAC core signal preparation.
 */

#include <string.h>

#include "resample.h"
#include "coder.h"
#include "util.h"

/* Anti-alias half-band FIR for the exact 2:1 decimation (RESAMPLE_FILTER_LEN
 * taps, equiripple, -6 dB at fs/4 = 12 kHz @48 k). A half-band filter has every
 * second tap equal to zero, so only the 32 even-indexed taps plus the centre
 * tap are non-zero: ~half the multiplies of a general FIR for the same length.
 *
 * The non-zero even taps are stored here in polyphase form: hb_even[m] is the
 * full filter's tap 2m. They are applied to the even-phase (decimated) input,
 * a unit-stride dot product that auto-vectorises (NEON/SSE) cleanly. The centre
 * tap multiplies the odd-phase sample directly.
 *
 * The wider (vs a general FIR) transition band only relaxes rejection ABOVE
 * ~12 kHz; the SBR crossover for every supported HE config sits at 7-9.5 kHz,
 * well below the flat (+/-0.05 dB) passband, and the residual alias energy folds
 * into the 11-12 kHz region that SBR re-synthesises over the core.
 * HB_CENTER: ideal half-band centre is 0.5; equiripple design settles at 0.5016
 * for equal stopband ripple with negligible passband deviation (<0.01 dB). */
#define HB_CENTER ((faac_real)0.5015570876767614)
static const faac_real hb_even[RESAMPLE_FILTER_LEN / 2 + 1] = {
    (faac_real)-2.39042884e-03f, (faac_real) 2.03978735e-03f, (faac_real)-2.88625768e-03f, (faac_real) 3.94878764e-03f,
    (faac_real)-5.26747336e-03f, (faac_real) 6.89408424e-03f, (faac_real)-8.89782634e-03f, (faac_real) 1.13774798e-02f,
    (faac_real)-1.44815390e-02f, (faac_real) 1.84491642e-02f, (faac_real)-2.36937924e-02f, (faac_real) 3.10005784e-02f,
    (faac_real)-4.20596122e-02f, (faac_real) 6.12815300e-02f, (faac_real)-1.04870415e-01f, (faac_real) 3.18777389e-01f,
    (faac_real) 3.18777389e-01f, (faac_real)-1.04870415e-01f, (faac_real) 6.12815300e-02f, (faac_real)-4.20596122e-02f,
    (faac_real) 3.10005784e-02f, (faac_real)-2.36937924e-02f, (faac_real) 1.84491642e-02f, (faac_real)-1.44815390e-02f,
    (faac_real) 1.13774798e-02f, (faac_real)-8.89782634e-03f, (faac_real) 6.89408424e-03f, (faac_real)-5.26747336e-03f,
    (faac_real) 3.94878764e-03f, (faac_real)-2.88625768e-03f, (faac_real) 2.03978735e-03f, (faac_real)-2.39042884e-03f,
};

Resampler *ResampleOpen(int channels)
{
    Resampler *r = (Resampler *)AllocMemory(sizeof(Resampler));
    if (!r) return NULL;
    SetMemory(r, 0, sizeof(Resampler));
    r->channels = channels;
    return r;
}

void ResampleClose(Resampler *r)
{
    FreeMemory(r);
}

int Resample2to1(Resampler *r, int input_len)
{
    int output_len = input_len / 2;
    const int H = RESAMPLE_FILTER_LEN - 1;            /* 62 */
    const int HALF = RESAMPLE_FILTER_LEN / 2;         /* 31 */
    const int NEVEN = RESAMPLE_FILTER_LEN / 2 + 1;    /* 32 even (non-zero) taps */
    int ch, i, j;

    for (ch = 0; ch < r->channels; ch++) {
        faac_real * __restrict in  = r->fullRate[ch];
        faac_real * __restrict out = r->halfRate[ch];
        faac_real * __restrict hist = r->buf[ch];

        /* Fixed-size buffers to avoid VLA (MSVC portability): history + one
         * full-rate HE frame (2 * FRAME_LEN input samples), plus the even-phase
         * (decimated) copy used for the unit-stride inner product. */
        faac_real combined[RESAMPLE_FILTER_LEN - 1 + 2 * FRAME_LEN];
        faac_real even[(RESAMPLE_FILTER_LEN - 1 + 2 * FRAME_LEN) / 2 + 1];
        int actual_input = input_len > 2 * FRAME_LEN ? 2 * FRAME_LEN : input_len;

        memcpy(combined,     hist, H            * sizeof(faac_real));
        memcpy(combined + H, in,   actual_input * sizeof(faac_real));

        /* Even-phase samples: even[t] = combined[2t]. Output i is then
         * sum_m hb_even[m]*even[i+m] (the non-zero even taps applied at unit
         * stride) plus the centre tap on the odd-phase sample combined[2i+31]. */
        int elen = (H + actual_input + 1) / 2;
        for (i = 0; i < elen; i++)
            even[i] = combined[2 * i];

        for (i = 0; i < output_len; i++) {
            const faac_real * __restrict e = even + i;
            faac_real a0 = 0, a1 = 0, a2 = 0, a3 = 0;
            for (j = 0; j < NEVEN; j += 4) {
                a0 += hb_even[j + 0] * e[j + 0];
                a1 += hb_even[j + 1] * e[j + 1];
                a2 += hb_even[j + 2] * e[j + 2];
                a3 += hb_even[j + 3] * e[j + 3];
            }
            *out++ = (a0 + a1) + (a2 + a3) + HB_CENTER * combined[2 * i + HALF];
        }

        memcpy(hist, combined + actual_input, H * sizeof(faac_real));
    }

    return output_len;
}
