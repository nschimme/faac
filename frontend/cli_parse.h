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

#ifndef FAAC_CLI_PARSE_H
#define FAAC_CLI_PARSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CLI_VERBOSITY_MAX 2

#ifdef __cplusplus
extern "C" {
#endif

bool cli_parse_uint(const char *text, unsigned long long max, unsigned long long *out);
bool cli_parse_verbosity(const char *text, uint8_t *out);
bool cli_parse_fps(const char *text, uint32_t *num, uint32_t *den);
bool cli_parse_chapter_line(const char *line, uint64_t *start_ms, char *title, size_t title_size);

#ifdef __cplusplus
}
#endif

#endif
