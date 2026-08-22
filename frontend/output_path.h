/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2001 Menno Bakker
 * Copyright (C) 2002-2017 Krzysztof Nikiel
 * Copyright (C) 2004 Dan Villiom P. Christiansen
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

#ifndef OUTPUT_PATH_H
#define OUTPUT_PATH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Generate default output filename based on input filename and container type */
char *get_output_filename(const char *input_filename, int container_mp4);

/* Check if filename extension suggests MP4 container format */
int is_mp4_filename(const char *filename);

#ifdef __cplusplus
}
#endif

#endif /* OUTPUT_PATH_H */
