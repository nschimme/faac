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

#ifdef __cplusplus
}
#endif

#endif /* PROGRESS_H */
