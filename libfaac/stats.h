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

#ifndef FAAC_STATS_H
#define FAAC_STATS_H

/* Deliberately standalone -- no other project headers, config.h included.
 * FAAC_STATS is passed as a direct -DFAAC_STATS compiler argument (see
 * meson.build) rather than routed through config.h, specifically so this
 * header -- included by instrumentation call sites across the encoder
 * instead of frame.h -- never needs config.h either. That matters for two
 * reasons: frame.h's much heavier transitive include set (coder.h,
 * channels.h, blockswitch.h, fft.h, sbr.h) would otherwise reach translation
 * units that have no need for it, and config.h itself carries unrelated
 * macros (e.g. HAVE_SSE2) that some .c files (quantize.c among them) don't
 * currently include on their own -- giving them accidental first-time
 * visibility into those changed their compiled behavior in a real, if
 * verified-harmless, way. Keeping this header free of any transitive
 * include avoids both. */
#ifdef FAAC_STATS

#ifdef __cplusplus
extern "C" {
#endif

typedef struct faacEncStats {
    unsigned int totalFrames;
    unsigned int transientFrames;
    double totalQuality;

    unsigned long totalBands;
    unsigned long msBands;
    unsigned long isBands;
    unsigned long pnsBands;

    unsigned int longBlocks;
    unsigned int longBlocksTNS;

    unsigned int sbrFrames;
    unsigned int sbrTransientFrames;
    unsigned long sbrInvfSum;
    unsigned long sbrInvfCount;

    double totalAttack;
    float maxAttack;
    unsigned long attackCount;

    unsigned int peakRetryFrames;

    double totalReservoirRatio;
    float minReservoirRatio;
    float maxReservoirRatio;
    unsigned int reservoirFrames;
} faacEncStats;

extern faacEncStats g_faacStats;

#ifdef __cplusplus
}
#endif

#endif /* FAAC_STATS */

#endif /* FAAC_STATS_H */
