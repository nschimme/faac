/*
 * Win32 Utilities
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

#ifndef WIN32_UTILS_H
#define WIN32_UTILS_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
/* Verify that the specified DLL is available on Windows.
   Returns true if available, false if missing.
   If missing and err_msg is provided, populates err_msg with a descriptive message. */
bool win32_check_dll_available(const char *dll_name, char *err_msg, size_t err_msg_len);
#endif

#ifdef __cplusplus
}
#endif

#endif /* WIN32_UTILS_H */
