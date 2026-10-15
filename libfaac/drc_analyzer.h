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

#ifndef DRC_ANALYZER_H
#define DRC_ANALYZER_H

#include <stdbool.h>
#include <stdint.h>
#include "drc.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Automatic Dynamic Range Control (DRC) Causal Analyzer
 * (ITU-R BS.1770-4 K-weighting + single-pass causal compression curve).
 */

typedef struct DrcAnalyzer DrcAnalyzer;

/*
 * Initialize the DRC analyzer for a given stream setup.
 *   sample_rate: full input PCM sample rate in Hz (e.g., 48000, 44100, 96000)
 *   num_channels: total input channel count
 *   is_lfe_channel: per-channel boolean array indicating LFE channels (or NULL)
 *   ref_level_lufs: target program reference level in LUFS (e.g., -23.0f)
 * Returns pointer to opaque DrcAnalyzer or NULL on allocation failure.
 */
DrcAnalyzer *DrcAnalyzerInit(uint32_t sample_rate, uint32_t num_channels,
                             const bool *is_lfe_channel, float ref_level_lufs);

/*
 * Process one frame of full-rate input PCM and produce the DRC gain metadata.
 *   da: analyzer instance
 *   input_pcm: array of per-channel pointers to float PCM samples
 *   samples_per_ch: number of input samples per channel in this frame
 *   out_drc: output DRCInfo struct to be populated
 */
void DrcAnalyzerProcessFrame(DrcAnalyzer *da, float * const *input_pcm,
                             uint32_t samples_per_ch, DRCInfo *out_drc);

/*
 * Destroy DRC analyzer instance and free associated resources.
 */
void DrcAnalyzerEnd(DrcAnalyzer *da);

#ifdef __cplusplus
}
#endif

#endif /* DRC_ANALYZER_H */
