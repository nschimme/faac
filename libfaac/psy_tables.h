/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2001 Menno Bakker
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

#ifndef PSY_TABLES_H
#define PSY_TABLES_H

#include <math.h>
#include "coder.h"

/* ISO 226:2003 parameters for ATH calculation */
typedef struct {
    float f;
    float af;
    float Lu;
    float Tf;
} ISO226_Params;

static const ISO226_Params iso226_table[] = {
    {20, 0.532, -31.6, 78.5},
    {25, 0.506, -27.2, 68.7},
    {31.5, 0.480, -23.0, 59.5},
    {40, 0.455, -19.1, 51.1},
    {50, 0.432, -15.9, 44.0},
    {63, 0.409, -13.0, 37.5},
    {80, 0.387, -10.3, 31.5},
    {100, 0.367, -8.1, 26.5},
    {125, 0.349, -6.2, 22.1},
    {160, 0.330, -4.5, 17.9},
    {200, 0.315, -3.1, 14.4},
    {250, 0.301, -2.0, 11.4},
    {315, 0.288, -1.1, 8.6},
    {400, 0.276, -0.4, 6.2},
    {500, 0.267, 0.0, 4.4},
    {630, 0.259, 0.3, 3.0},
    {800, 0.253, 0.5, 2.2},
    {1000, 0.250, 0.0, 2.4},
    {1250, 0.246, -2.7, 3.5},
    {1600, 0.244, -4.1, 1.7},
    {2000, 0.243, -1.0, -1.3},
    {2500, 0.243, 1.7, -4.2},
    {3150, 0.243, 2.5, -6.0},
    {4000, 0.242, 1.2, -5.4},
    {5000, 0.242, -2.1, -1.5},
    {6300, 0.245, -7.1, 6.0},
    {8000, 0.254, -11.2, 12.6},
    {10000, 0.271, -10.7, 13.9},
    {12500, 0.301, -3.1, 12.3}
};

static float bark_from_hz(float hz)
{
    return 13.0f * atanf(0.00076f * hz) + 3.5f * atanf(powf(hz / 7500.0f, 2.0f));
}

/* Alternative formula provided in the task: 26.81 * hz / (1960 + hz) - 0.53 */
static float bark_from_hz_task(float hz)
{
    return 26.81f * hz / (1960.0f + hz) - 0.53f;
}

static float interpolate_ath(float hz)
{
    int i;
    int n = sizeof(iso226_table) / sizeof(iso226_table[0]);

    if (hz <= iso226_table[0].f) return iso226_table[0].Tf;
    if (hz >= iso226_table[n-1].f) return iso226_table[n-1].Tf;

    for (i = 0; i < n - 1; i++) {
        if (hz >= iso226_table[i].f && hz <= iso226_table[i+1].f) {
            float frac = (hz - iso226_table[i].f) / (iso226_table[i+1].f - iso226_table[i].f);
            return iso226_table[i].Tf + frac * (iso226_table[i+1].Tf - iso226_table[i].Tf);
        }
    }
    return 0.0f;
}

typedef struct {
    float sfb_bark[NSFB_LONG];
    float sfb_ath[NSFB_LONG];
    float spread_low[NSFB_LONG][NSFB_LONG];
    float sfb_bark_s[NSFB_SHORT];
    float sfb_ath_s[NSFB_SHORT];
    float spread_short[NSFB_SHORT][NSFB_SHORT];
} PsyTable;

static void precompute_psy_tables(PsyTable *table, SR_INFO *sr)
{
    int sfb;
    int offset;

    /* Long windows */
    offset = 0;
    for (sfb = 0; sfb < sr->num_cb_long; sfb++) {
        float f = (float)sr->sampling_rate * (offset + sr->cb_width_long[sfb] / 2.0f) / (float)BLOCK_LEN_LONG;
        table->sfb_bark[sfb] = bark_from_hz_task(f);
        table->sfb_ath[sfb] = interpolate_ath(f);
        offset += sr->cb_width_long[sfb];
    }

    /* Short windows */
    offset = 0;
    for (sfb = 0; sfb < sr->num_cb_short; sfb++) {
        float f = (float)sr->sampling_rate * (offset + sr->cb_width_short[sfb] / 2.0f) / (float)BLOCK_LEN_SHORT;
        table->sfb_bark_s[sfb] = bark_from_hz_task(f);
        table->sfb_ath_s[sfb] = interpolate_ath(f);
        offset += sr->cb_width_short[sfb];
    }

    /* Spreading matrices */
    for (int i = 0; i < sr->num_cb_long; i++) {
        for (int j = 0; j < sr->num_cb_long; j++) {
            float dz = table->sfb_bark[i] - table->sfb_bark[j];
            float weight_db = 15.811389f + 7.5f * (dz + 0.474f) - 17.5f * sqrtf(1.0f + powf(dz + 0.474f, 2.0f));
            table->spread_low[i][j] = powf(10.0f, weight_db / 10.0f);
        }
    }
    for (int i = 0; i < sr->num_cb_short; i++) {
        for (int j = 0; j < sr->num_cb_short; j++) {
            float dz = table->sfb_bark_s[i] - table->sfb_bark_s[j];
            float weight_db = 15.811389f + 7.5f * (dz + 0.474f) - 17.5f * sqrtf(1.0f + powf(dz + 0.474f, 2.0f));
            table->spread_short[i][j] = powf(10.0f, weight_db / 10.0f);
        }
    }
}

#endif
