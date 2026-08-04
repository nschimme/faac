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
 */

#include "tuning.h"

#ifdef FAAC_TUNING

#include <stdio.h>
#include <stdlib.h>

FaacTuning faacTuning;

static float env_f(const char *name)
{
    const char *e = getenv(name);
    char *end;
    double v;

    if (!e || !*e)
        return -1.0f;
    v = strtod(e, &end);
    /* Reject junk rather than silently tuning to 0: a typo'd sweep point that
     * quietly becomes "threshold 0" produces a plausible-looking number. */
    if (end == e || v < 0.0)
        return -1.0f;
    return (float)v;
}

static int env_i(const char *name)
{
    float v = env_f(name);
    return (v < 0.0f) ? -1 : (int)v;
}

static int env_flag(const char *name)
{
    const char *e = getenv(name);
    return e && e[0] == '1';
}

void FaacTuningInit(void)
{
    static int done = 0;

    if (done)
        return;
    done = 1;

    faacTuning.td_thresh      = env_f("FAAC_TD_THRESH");
    faacTuning.tns_gain       = env_f("FAAC_TNS_GAIN");
    faacTuning.tns_gain_clamp = env_f("FAAC_TNS_GAIN_CLAMP");
    faacTuning.tns_measured   = env_f("FAAC_TNS_MEASURED");
    faacTuning.tns_sfm        = env_f("FAAC_TNS_SFM");
    faacTuning.tns_order      = env_i("FAAC_TNS_ORDER");
    faacTuning.tns_dir        = env_i("FAAC_TNS_DIR");
    faacTuning.tns_attack     = env_f("FAAC_TNS_ATTACK");
    faacTuning.sbr_td_thresh  = env_f("FAAC_SBR_TD_THRESH");
    faacTuning.force_long     = env_flag("FAAC_FORCE_LONG");
    faacTuning.bs_stats       = env_flag("FAAC_BS_STATS");
    faacTuning.tns_stats      = env_flag("FAAC_TNS_STATS");
    faacTuning.psy_stats      = env_flag("FAAC_PSY_STATS");
    faacTuning.psy_global     = env_f("FAAC_PSY_GLOBAL");
    faacTuning.psy_avg_floor  = env_f("FAAC_PSY_AVG_FLOOR");
    faacTuning.psy_peak_floor = env_f("FAAC_PSY_PEAK_FLOOR");
    faacTuning.psy_loudness   = env_f("FAAC_PSY_LOUDNESS");
    faacTuning.psy_avg_weight = env_f("FAAC_PSY_AVG_WEIGHT");
    faacTuning.psy_startstop  = env_f("FAAC_PSY_STARTSTOP");
    faacTuning.psy_short      = env_f("FAAC_PSY_SHORT");

    /* Announce, so a sweep run against a non-tuning binary -- which would
     * ignore every knob and report a clean "no difference" -- is detectable. */
    fprintf(stderr, "FAAC_TUNING build (env knobs active)\n");
}

#else /* !FAAC_TUNING: ISO C forbids an empty translation unit. */
typedef int faacTuningCompiledOut;
#endif
