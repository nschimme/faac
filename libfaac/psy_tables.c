/****************************************************************************
    Psychoacoustic lookup tables — implementation.

    Copyright (C) 2026 FAAC contributors

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
****************************************************************************/

#include <math.h>
#include "psy_tables.h"

float ath_cb[64];
float sfb_bark  [12][NSFB_LONG];
float sfb_ath   [12][NSFB_LONG];
float sfb_bark_s[12][NSFB_SHORT];
float sfb_ath_s [12][NSFB_SHORT];

void psy_tables_init(const SR_INFO *sr_table, int n)
{
    static int initialized = 0;
    int i, sr_idx, sfb;

    if (initialized)
        return;
    initialized = 1;

    /* --- Fill ath_cb[64] ---
     * f[i] = i * 24000 / 63 Hz.
     * Formula: Painter & Spanias 2000 (ISO 226 approximation)
     *   ATH(f) = 3.64 * (f_khz)^-0.8
     *          - 6.5  * exp(-0.6 * (f_khz - 3.3)^2)
     *          + 1e-3 * (f_khz)^4
     */
    for (i = 0; i < 64; i++) {
        float f_hz = i * (24000.0f / 63.0f);
        float val;

        if (f_hz < 1.0f) {
            /* DC / sub-Hz: hearing threshold is very high */
            val = 60.0f;
        } else {
            float fk  = f_hz * 0.001f;     /* Hz → kHz */
            float fk2 = fk - 3.3f;
            val = 3.64f  * powf(fk, -0.8f)
                - 6.5f   * expf(-0.6f * fk2 * fk2)
                + 1e-3f  * fk * fk * fk * fk;

            if (val < -20.0f) val = -20.0f;
            if (val > 120.0f) val = 120.0f;
        }
        ath_cb[i] = val;
    }

    /* --- Fill sfb_bark / sfb_ath (long window) ---
     * mid_hz = mid_line * sr / (2 * BLOCK_LEN_LONG)
     */
    for (sr_idx = 0; sr_idx < n; sr_idx++) {
        float sr   = (float)sr_table[sr_idx].sampling_rate;
        int nbands = sr_table[sr_idx].num_cb_long;
        int offset = 0;

        for (sfb = 0; sfb < NSFB_LONG; sfb++) {
            if (sfb < nbands) {
                int   w        = sr_table[sr_idx].cb_width_long[sfb];
                float mid_line = (float)offset + (float)w * 0.5f;
                float mid_hz   = mid_line * sr / (2.0f * (float)BLOCK_LEN_LONG);

                sfb_bark[sr_idx][sfb] = bark_from_hz(mid_hz);
                sfb_ath [sr_idx][sfb] = ath_from_hz(mid_hz);
                offset += w;
            } else {
                sfb_bark[sr_idx][sfb] = 0.0f;
                sfb_ath [sr_idx][sfb] = 0.0f;
            }
        }
    }

    /* --- Fill sfb_bark_s / sfb_ath_s (short window) ---
     * mid_hz = mid_line * sr / (2 * BLOCK_LEN_SHORT)
     */
    for (sr_idx = 0; sr_idx < n; sr_idx++) {
        float sr    = (float)sr_table[sr_idx].sampling_rate;
        int nbands  = sr_table[sr_idx].num_cb_short;
        int offset  = 0;

        for (sfb = 0; sfb < NSFB_SHORT; sfb++) {
            if (sfb < nbands) {
                int   w        = sr_table[sr_idx].cb_width_short[sfb];
                float mid_line = (float)offset + (float)w * 0.5f;
                float mid_hz   = mid_line * sr / (2.0f * (float)BLOCK_LEN_SHORT);

                sfb_bark_s[sr_idx][sfb] = bark_from_hz(mid_hz);
                sfb_ath_s [sr_idx][sfb] = ath_from_hz(mid_hz);
                offset += w;
            } else {
                sfb_bark_s[sr_idx][sfb] = 0.0f;
                sfb_ath_s [sr_idx][sfb] = 0.0f;
            }
        }
    }
}
