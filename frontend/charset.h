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

#ifndef CHARSET_H
#define CHARSET_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Ensure string is valid UTF-8, converting from system encoding if needed */
char *utf8_ensure(const char *str);

#ifdef _WIN32
#include <windows.h>
/* Convert UTF-16 wchar_t string to heap-allocated UTF-8 string */
char *win32_utf16_to_utf8(const wchar_t *wstr);
#endif

#ifdef __cplusplus
}
#endif

#endif /* CHARSET_H */
