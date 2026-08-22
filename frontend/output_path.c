/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2001 Menno Bakker
 * Copyright (C) 2002-2026 FAAC Team
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "output_path.h"

#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

char *get_output_filename(const char *input_filename, int container_mp4)
{
    if (!input_filename)
        return NULL;

    const char *ext = container_mp4 ? ".m4a" : ".aac";
    const char *dot = strrchr(input_filename, '.');
    size_t len = dot ? (size_t)(dot - input_filename) : strlen(input_filename);

    char *aac_file_name = malloc(len + 5);
    if (aac_file_name)
    {
        memcpy(aac_file_name, input_filename, len);
        memcpy(aac_file_name + len, ext, 5);
    }
    return aac_file_name;
}

int is_mp4_filename(const char *filename)
{
    if (!filename)
        return 0;

    const char *ext = strrchr(filename, '.');
    if (ext)
    {
        if (!strcasecmp(ext, ".m4a") || !strcasecmp(ext, ".mp4") || !strcasecmp(ext, ".m4b"))
            return 1;
    }
    return 0;
}
