/*
 * FAAR - Freeware Advanced Audio Resampler
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
#ifndef FAAR_ENGINE_H
#define FAAR_ENGINE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <faar.h>
#include "input.h"

typedef enum {
    FAAR_PCM_F32LE = 0,
    FAAR_PCM_S16LE = 1,
    FAAR_PCM_S24LE = 2
} faar_pcm_format_t;

/* Filter designs shared by the streams of one batch, one per distinct input
   rate and channel count. Optional; the engine opens plain resamplers
   without it. */
typedef struct faar_design_cache faar_design_cache;
faar_design_cache *faar_design_cache_new(void);
void faar_design_cache_free(faar_design_cache *cache);

typedef struct {
    uint32_t output_rate;
    faar_quality quality;
    faar_pcm_format_t format;
    uint32_t dither; /* FAAR_DITHER_TPDF or FAAR_DITHER_SHAPED; 0 for none */
    uint32_t decimate_factor;
    uint8_t verbosity;
    bool write_wav;
    faar_design_cache *designs; /* may be NULL */
} faar_engine_options_t;

typedef struct {
    uint64_t input_frames;
    uint64_t output_frames;
    double elapsed_seconds;
} faar_engine_stats_t;

/* Process an opened input stream into an opened output stream. The caller
   retains ownership of both streams. Returns zero on success. */
int faar_engine_run(pcmfile_t *input, FILE *output,
                    const faar_engine_options_t *options,
                    faar_engine_stats_t *stats);

#endif
