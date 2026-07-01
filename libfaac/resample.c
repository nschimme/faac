/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2026 Nils Schimmelmann
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
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
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
#define HB_CENTER 0.5015570876767614f
static const resfloat hb_even[RESAMPLE_FILTER_LEN / 2 + 1] = {
    -2.39042884e-03f,  2.03978735e-03f, -2.88625768e-03f,  3.94878764e-03f,
    -5.26747336e-03f,  6.89408424e-03f, -8.89782634e-03f,  1.13774798e-02f,
    -1.44815390e-02f,  1.84491642e-02f, -2.36937924e-02f,  3.10005784e-02f,
    -4.20596122e-02f,  6.12815300e-02f, -1.04870415e-01f,  3.18777389e-01f,
     3.18777389e-01f, -1.04870415e-01f,  6.12815300e-02f, -4.20596122e-02f,
     3.10005784e-02f, -2.36937924e-02f,  1.84491642e-02f, -1.44815390e-02f,
     1.13774798e-02f, -8.89782634e-03f,  6.89408424e-03f, -5.26747336e-03f,
     3.94878764e-03f, -2.88625768e-03f,  2.03978735e-03f, -2.39042884e-03f,
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
    int ch, i, j;

    for (ch = 0; ch < r->channels; ch++) {
        faac_real * __restrict in  = r->fullRate[ch];
        faac_real * __restrict out = r->halfRate[ch];
        faac_real * __restrict hist = r->buf[ch];

        /* Fixed-size buffers to avoid VLA (MSVC portability): history + one
         * full-rate HE frame (2 * FRAME_LEN input samples). */
        faac_real combined[RESAMPLE_FILTER_LEN - 1 + 2 * FRAME_LEN];

        memcpy(combined,     hist, H         * sizeof(faac_real));
        memcpy(combined + H, in,   input_len * sizeof(faac_real));

        /* Symmetry: hb_even[j] == hb_even[31-j]. Output i is then
         * sum_{j<16} hb_even[j]*(combined[2i+2j] + combined[2i+2(31-j)])
         * plus the centre tap on the odd-phase sample combined[2i+31]. */
        for (i = 0; i < output_len; i++) {
            const faac_real * __restrict c = combined + 2 * i;
            faac_real a0 = 0, a1 = 0, a2 = 0, a3 = 0;
            for (j = 0; j < 16; j += 4) {
                a0 += hb_even[j + 0] * (c[2 * (j + 0)] + c[2 * (31 - j - 0)]);
                a1 += hb_even[j + 1] * (c[2 * (j + 1)] + c[2 * (31 - j - 1)]);
                a2 += hb_even[j + 2] * (c[2 * (j + 2)] + c[2 * (31 - j - 2)]);
                a3 += hb_even[j + 3] * (c[2 * (j + 3)] + c[2 * (31 - j - 3)]);
            }
            *out++ = (a0 + a1) + (a2 + a3) + HB_CENTER * combined[2 * i + HALF];
        }

        memcpy(hist, combined + input_len, H * sizeof(faac_real));
    }

    return output_len;
}
