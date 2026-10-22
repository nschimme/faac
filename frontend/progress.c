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

#include "progress.h"

#include <stdio.h>

void progress_throttle_reset(progress_throttle_t *t)
{
    t->last_fired_sec = -1.0;
    t->reached_end = false;
}

bool progress_throttle_tick(progress_throttle_t *t, const progress_info_t *info,
                             double interval_sec)
{
    bool at_end = info->total_input_samples > 0 &&
                  info->current_input_samples >= info->total_input_samples;
    bool fire_edge = at_end && !t->reached_end;
    if (at_end)
        t->reached_end = true;

    /* Avoid drawing the startup row with zero elapsed time/speed. Final
       progress is always delivered, including for sessions shorter than the
       throttle interval. */
    bool elapsed_ready = info->time_elapsed_sec >= interval_sec;
    bool fire = info->is_final ||
                (elapsed_ready && (fire_edge || t->last_fired_sec < 0.0 ||
                 (info->time_elapsed_sec - t->last_fired_sec) >= interval_sec));
    if (fire)
        t->last_fired_sec = info->time_elapsed_sec;
    return fire;
}

void progress_render_cli(const progress_info_t *info)
{
    if (info->total_frames > 0) {
        uint64_t percent = (uint64_t)info->current_frame * 100 / info->total_frames;
        if (percent > 100) percent = 100;
        double played_sec = (double)info->current_input_samples /
                            (double)(info->sample_rate ? info->sample_rate : 1);
        double bitrate_kbps = played_sec > 0.0 ?
            ((double)info->total_bytes_written * 8.0 / 1000.0) / played_sec : 0.0;
        fprintf(stderr, "\r%7u/%-7u (%3d%%) | %7.1f | %6.1f/%-6.1f | %7.2fx | %.1f ",
                info->current_frame, info->total_frames, (int)percent, bitrate_kbps,
                info->time_elapsed_sec, info->time_elapsed_sec + info->eta_sec,
                info->speed_factor, info->eta_sec);
    } else {
        fprintf(stderr, "\r %7u | %7.1f | %7.2fx ", info->current_frame,
                info->time_elapsed_sec, info->speed_factor);
    }
    fflush(stderr);
    if (info->is_final) fputc('\n', stderr);
}
