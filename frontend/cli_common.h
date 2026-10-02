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

#ifndef CLI_COMMON_H
#define CLI_COMMON_H

#ifdef _WIN32
#include "charset.h"
#else
#include <stdio.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* fopen() a UTF-8 path: win32_fopen_utf8() on Windows, plain fopen()
   elsewhere. Same open-failure semantics as fopen() either way -- callers
   still check the returned NULL and report the error themselves. */
FILE *cli_fopen(const char *path, const char *mode);

/* Format a CLI version with the build's short Git revision when available. */
const char *cli_version_string(char *buf, size_t buf_size, const char *version);

/* Print project attribution and the shared LGPL-2.1-or-later notice. */
void cli_print_license(const char *name, const char *copyright);
void cli_print_lgpl_notice(FILE *stream, const char *subject);

#ifdef __cplusplus
}
#endif

#endif /* CLI_COMMON_H */
