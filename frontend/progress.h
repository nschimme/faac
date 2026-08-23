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

#ifndef PROGRESS_H
#define PROGRESS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t current_input_samples;
    uint64_t total_input_samples;
    uint32_t sample_rate;
    uint16_t num_channels;
    uint32_t current_frame;
    uint32_t total_frames;
    uint64_t total_bytes_written;
    double time_elapsed_sec;
    double speed_factor;
    double eta_sec;
} progress_info_t;

/* Callback function type for reporting progress updates.
   Return false from callback to signal user-cancelled encoding. */
typedef bool (*progress_callback_t)(const progress_info_t *info, void *user_data);

/* Shared rate-limiting for progress_callback_t implementations: the caller
   (encode_engine.c) invokes progress_cb every frame so cancellation is
   checked promptly, but redrawing a status line or posting a UI message
   that often is wasted work. progress_throttle_tick() answers "should I
   actually update now?" at an interval, plus exactly once on the frame
   where current_input_samples reaches total_input_samples -- that value
   stays pinned across every post-EOF flush frame, so this must be an edge
   trigger (rising edge only) rather than a level check, or every flush
   frame after the first would fire again. */
typedef struct {
    double last_fired_sec;
    bool reached_end;
} progress_throttle_t;

static inline void progress_throttle_reset(progress_throttle_t *t)
{
    t->last_fired_sec = -1.0;
    t->reached_end = false;
}

static inline bool progress_throttle_tick(progress_throttle_t *t,
                                           const progress_info_t *info,
                                           double interval_sec)
{
    bool at_end = info->total_input_samples > 0 &&
                  info->current_input_samples >= info->total_input_samples;
    bool fire_edge = at_end && !t->reached_end;
    if (at_end)
        t->reached_end = true;

    bool fire = fire_edge || t->last_fired_sec < 0.0 ||
                (info->time_elapsed_sec - t->last_fired_sec) >= interval_sec;
    if (fire)
        t->last_fired_sec = info->time_elapsed_sec;
    return fire;
}

#ifdef __cplusplus
}
#endif

#endif /* PROGRESS_H */
