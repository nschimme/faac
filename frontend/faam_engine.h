/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2026 Nils Schimmelmann
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#ifndef FAAM_ENGINE_H
#define FAAM_ENGINE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "faam.h"
#include "adts.h"
#include "annexb.h"
#include "progress.h"

#define FAAM_VIDEO_TIMESCALE 90000
#define FAAM_ENGINE_CANCELLED 1

typedef bool (*faam_engine_demux_sample_fn)(void *user, const faam_frame_loc *loc,
                                           const uint8_t *frame, uint32_t frame_bytes);

faam_status faam_engine_mux_adts_frames(faam_muxer *m, uint32_t track_id, FILE *f,
                                        const adts_params *first, const uint8_t *prefix,
                                        size_t prefix_len, uint32_t *frames, bool *multi_block,
                                        progress_callback_t progress, void *progress_user);
faam_status faam_engine_mux_video_frames(faam_muxer *m, uint32_t track_id, annexb_codec codec,
                                         const uint8_t *data, size_t len, uint32_t fps_num,
                                         uint32_t fps_den, uint32_t *frames,
                                         progress_callback_t progress, void *progress_user);
faam_status faam_engine_demux_track(faam_demuxer *d, const faam_track_info *track,
                                    faam_engine_demux_sample_fn sample, void *sample_user,
                                    progress_callback_t progress, void *progress_user);

#endif
