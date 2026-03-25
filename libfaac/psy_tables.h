/****************************************************************************
    Psychoacoustic lookup tables: ATH, Bark scale, per-SFB precomputed values.

    Copyright (C) 2026 FAAC contributors

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    References:
      Painter & Spanias, "Perceptual Coding of Digital Audio",
        Proc. IEEE, 2000.
      Traunmüller, "Analytical expressions for the tonotopic sensory scale",
        JASA 88(1), 1990.
****************************************************************************/

#ifndef PSY_TABLES_H
#define PSY_TABLES_H

#include <math.h>
#include "coder.h"   /* SR_INFO, NSFB_LONG, BLOCK_LEN_LONG */

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------
 * 1.  Absolute Threshold of Hearing
 *     64-point table, f[i] = i * 24000 / 63 Hz, i = 0..63
 *     Values in dB SPL, Painter & Spanias 2000 formula:
 *       ATH(f) = 3.64*(f/1000)^-0.8 - 6.5*exp(-0.6*(f/1000-3.3)^2)
 *                + 1e-3*(f/1000)^4
 *     Clamped to [-20, 120] dB.  f=0 is special-cased to 60 dB.
 *     Filled by psy_tables_init() — do not read before that call.
 * ------------------------------------------------------------------ */
extern float ath_cb[64];

/* ------------------------------------------------------------------
 * 2.  Bark-frequency conversion — Traunmüller 1990
 *     Returns the Bark value for a given frequency in Hz.
 * ------------------------------------------------------------------ */
static inline float bark_from_hz(float hz)
{
    return 26.81f * hz / (1960.0f + hz) - 0.53f;
}

/* ------------------------------------------------------------------
 * 3.  ATH interpolation helper
 *     Linear interpolation into ath_cb[64].
 *     Only valid after psy_tables_init() has been called.
 * ------------------------------------------------------------------ */
static inline float ath_from_hz(float hz)
{
    float idx = hz * (63.0f / 24000.0f);
    int   lo  = (int)idx;
    float fr;
    if (lo < 0)   return ath_cb[0];
    if (lo >= 63) return ath_cb[63];
    fr = idx - (float)lo;
    return ath_cb[lo] * (1.0f - fr) + ath_cb[lo + 1] * fr;
}

/* ------------------------------------------------------------------
 * 4.  Per-sample-rate SFB tables
 *     Long-window: [sr_idx 0..11][sfb 0..NSFB_LONG-1]
 *     Short-window: [sr_idx 0..11][sfb 0..NSFB_SHORT-1]
 *     sr_idx matches GetSRIndex() in util.c.
 *     Entries beyond num_cb_long / num_cb_short for that rate are 0.
 *     Filled by psy_tables_init() — do not read before that call.
 * ------------------------------------------------------------------ */
extern float sfb_bark  [12][NSFB_LONG];  /* Bark of long-window SFB midpoint */
extern float sfb_ath   [12][NSFB_LONG];  /* ATH (dB SPL) at long-window SFB midpoint */
extern float sfb_bark_s[12][NSFB_SHORT]; /* Bark of short-window SFB midpoint */
extern float sfb_ath_s [12][NSFB_SHORT]; /* ATH (dB SPL) at short-window SFB midpoint */

/* ------------------------------------------------------------------
 * 5.  Initialiser
 *     Call once from faacEncOpen() after hEncoder->srInfo is set.
 *     sr_table must be the full 12-entry srInfo[] array from frame.c.
 *     Idempotent: subsequent calls are no-ops.
 * ------------------------------------------------------------------ */
void psy_tables_init(const SR_INFO *sr_table, int n);

#ifdef __cplusplus
}
#endif

#endif /* PSY_TABLES_H */
