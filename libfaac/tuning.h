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

/*
 * Measurement-only threshold overrides, read once from the environment.
 *
 * Perceptual thresholds can only be chosen from paired A/B measurements, and a
 * sweep that needs a rebuild per point is a sweep nobody runs. These knobs let
 * ../faac-benchmark's score_preecho.py --env-ab vary one threshold at a time
 * against a fixed binary.
 *
 * Compiled out unless -Dtuning=true, so shipped builds carry no getenv() and no
 * env-dependent encoder behaviour. A tuning build prints a banner to stderr on
 * first init, so a harness can refuse to sweep a binary that would silently
 * ignore every knob and report a clean "no difference".
 */

#ifndef TUNING_H
#define TUNING_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef FAAC_TUNING

/* Negative means "not overridden"; every knob here is a positive quantity. */
typedef struct {
    float td_thresh;        /* FAAC_TD_THRESH       PSY_TD_THRESH */
    float tns_gain;         /* FAAC_TNS_GAIN        TNS_GAIN_LIMIT */
    float tns_gain_clamp;   /* FAAC_TNS_GAIN_CLAMP  upper gain bound (default: none) */
    float tns_measured;     /* FAAC_TNS_MEASURED    TNS_MEASURED_GAIN */
    float tns_sfm;          /* FAAC_TNS_SFM         TNS_PNS_SFM_SKIP */
    int   tns_order;        /* FAAC_TNS_ORDER       TNS_LPC_ORDER */
    int   tns_dir;          /* FAAC_TNS_DIR         filter direction (0/1/2/3) */
    int   tns_filters;      /* FAAC_TNS_FILTERS     filters per long window */
    float tns_attack;       /* FAAC_TNS_ATTACK      temporal admission gate */
    float sbr_td_thresh;    /* FAAC_SBR_TD_THRESH   HE-AAC transient threshold */
    int   force_long;       /* FAAC_FORCE_LONG      block switching off */
    int   bs_stats;         /* FAAC_BS_STATS        short-block rate to stderr */
    int   tns_stats;        /* FAAC_TNS_STATS       TNS gate funnel to stderr */
    int   psy_stats;        /* FAAC_PSY_STATS       masking-model census to stderr */
    float psy_global;       /* FAAC_PSY_GLOBAL      global masking-target scale */
    float psy_avg_floor;    /* FAAC_PSY_AVG_FLOOR   AVG_ENERGY_FLOOR_FRAC */
    float psy_peak_floor;   /* FAAC_PSY_PEAK_FLOOR  PEAK_ENERGY_FLOOR_FRAC */
    float psy_loudness;     /* FAAC_PSY_LOUDNESS    LOUDNESS_EXPONENT */
    float psy_avg_weight;   /* FAAC_PSY_AVG_WEIGHT  AVG_ENERGY_WEIGHT */
    float psy_startstop;    /* FAAC_PSY_STARTSTOP   START/STOP window tightening */
    float psy_short;        /* FAAC_PSY_SHORT      short-block tightening */
} FaacTuning;

extern FaacTuning faacTuning;

/* Idempotent; called from PsyInit and TnsInit so every encoder open is covered.
 * Concurrent opens can race on the init guard, but both racers write identical
 * values, and this build is not the one anybody ships. */
void FaacTuningInit(void);

/* Apply an override to a local holding the compiled-in default. */
#define FAAC_TUNE_F(lval, field) \
    do { if (faacTuning.field >= 0.0f) (lval) = faacTuning.field; } while (0)
#define FAAC_TUNE_I(lval, field) \
    do { if (faacTuning.field >= 0) (lval) = faacTuning.field; } while (0)
#define FAAC_TUNE_ON(field) (faacTuning.field > 0)

#else /* !FAAC_TUNING */

#define FaacTuningInit() ((void)0)
#define FAAC_TUNE_F(lval, field) ((void)0)
#define FAAC_TUNE_I(lval, field) ((void)0)
#define FAAC_TUNE_ON(field) 0

#endif

#ifdef __cplusplus
}
#endif

#endif /* TUNING_H */
