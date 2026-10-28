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

/* Compile-time guard for the public struct ABI: every extensible struct starts
 * with struct_size and its minimum accepted size (baseline) fits inside it. The
 * baselines of the pointer-free structs are exact because they are identical on
 * ILP32, LP64 and LLP64, big or little endian. Growing a struct appends past
 * these values; the numbers below change only with a SONAME bump. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <faac.h>
#include <faad3.h>
#include <faar.h>
#include <faam.h>

#define FIRST(T) _Static_assert(offsetof(T, struct_size) == 0, #T " must start with struct_size")
#define BASELINE(T, B) _Static_assert((B) > sizeof(uint32_t) && (B) <= sizeof(T), #T " baseline")
#define EXACT(T, N) _Static_assert(sizeof(T) == (N), #T " size changed")

FIRST(faac_params); FIRST(faac_encoder_info); FIRST(faac_library_info);
FIRST(faad_config); FIRST(faad_stream_info); FIRST(faad_library_info);
FIRST(faar_config); FIRST(faar_library_info); FIRST(faar_resampler_info);
FIRST(faam_io); FIRST(faam_gapless_info); FIRST(faam_chapter);
FIRST(faam_track_config); FIRST(faam_track_info); FIRST(faam_frame_loc);
FIRST(faam_metadata_entry); FIRST(faam_demuxer_config); FIRST(faam_muxer_config);
FIRST(faam_muxer_update_params); FIRST(faam_muxer_info); FIRST(faam_library_info);

BASELINE(faac_params, FAAC_PARAMS_BASELINE);
BASELINE(faac_encoder_info, FAAC_ENCODER_INFO_BASELINE);
BASELINE(faac_library_info, FAAC_LIBRARY_INFO_BASELINE);
BASELINE(faad_config, FAAD_CONFIG_BASELINE);
BASELINE(faad_stream_info, FAAD_STREAM_INFO_BASELINE);
BASELINE(faad_library_info, FAAD_LIBRARY_INFO_BASELINE);
BASELINE(faar_config, FAAR_CONFIG_BASELINE);
BASELINE(faar_library_info, FAAR_LIBRARY_INFO_BASELINE);
BASELINE(faar_resampler_info, FAAR_RESAMPLER_INFO_BASELINE);
BASELINE(faam_io, FAAM_IO_BASELINE);
BASELINE(faam_gapless_info, FAAM_GAPLESS_INFO_BASELINE);
BASELINE(faam_chapter, FAAM_CHAPTER_BASELINE);
BASELINE(faam_track_config, FAAM_TRACK_CONFIG_BASELINE);
BASELINE(faam_track_info, FAAM_TRACK_INFO_BASELINE);
BASELINE(faam_frame_loc, FAAM_FRAME_LOC_BASELINE);
BASELINE(faam_metadata_entry, FAAM_METADATA_ENTRY_BASELINE);
BASELINE(faam_demuxer_config, FAAM_DEMUXER_CONFIG_BASELINE);
BASELINE(faam_muxer_config, FAAM_MUXER_CONFIG_BASELINE);
BASELINE(faam_muxer_update_params, FAAM_MUXER_UPDATE_PARAMS_BASELINE);
BASELINE(faam_muxer_info, FAAM_MUXER_INFO_BASELINE);
BASELINE(faam_library_info, FAAM_LIBRARY_INFO_BASELINE);

/* Pointer-free structs: released baselines and offsets are identical on every target. */
#define BASELINE_IS(B, N) _Static_assert((B) == (N), #B " changed")
BASELINE_IS(FAAC_PARAMS_BASELINE, 60);
BASELINE_IS(FAAC_ENCODER_INFO_BASELINE, 48);
BASELINE_IS(FAAD_CONFIG_BASELINE, 16);
BASELINE_IS(FAAD_STREAM_INFO_BASELINE, 36);
BASELINE_IS(FAAR_CONFIG_BASELINE, 32);
BASELINE_IS(FAAR_RESAMPLER_INFO_BASELINE, 12);
BASELINE_IS(FAAM_DEMUXER_CONFIG_BASELINE, 8);
_Static_assert(offsetof(faac_params, sample_rate) == 4 && offsetof(faac_params, rate_control) == 56,
               "faac_params layout");
_Static_assert(offsetof(faad_config, downmix_mode) == 12, "faad_config layout");

/* Pointer-free structs stay 4-byte aligned so a caller's smaller struct is never mis-aligned. */
#define ALIGN4(T) _Static_assert(_Alignof(T) == 4, #T " alignment")
ALIGN4(faac_params); ALIGN4(faac_encoder_info); ALIGN4(faad_config); ALIGN4(faad_stream_info);
ALIGN4(faar_config); ALIGN4(faar_resampler_info); ALIGN4(faam_demuxer_config);

int main(void)
{
    return 0;
}
