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
#define HB_CENTER ((faac_real)0.5015570876767614)
static const faac_real hb_even_compact[16] = {
    (faac_real)-2.3904288400e-03f, (faac_real)+2.0397873500e-03f, (faac_real)-2.8862576800e-03f, (faac_real)+3.9487876400e-03f,
    (faac_real)-5.2674733600e-03f, (faac_real)+6.8940842400e-03f, (faac_real)-8.8978263400e-03f, (faac_real)+1.1377479800e-02f,
    (faac_real)-1.4481539000e-02f, (faac_real)+1.8449164200e-02f, (faac_real)-2.3693792400e-02f, (faac_real)+3.1000578400e-02f,
    (faac_real)-4.2059612200e-02f, (faac_real)+6.1281530000e-02f, (faac_real)-1.0487041500e-01f, (faac_real)+3.1877738900e-01f
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

    faac_real hb[32];
    for (i = 0; i < 16; i++) { hb[i] = hb_even_compact[i]; hb[31-i] = hb_even_compact[i]; }

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
            faac_real sum = 0;
            for (j = 0; j < NEVEN; j++) sum += hb[j] * e[j];
            *out++ = sum + HB_CENTER * combined[2 * i + HALF];
        }

        memcpy(hist, combined + actual_input, H * sizeof(faac_real));
    }

    return output_len;
}
