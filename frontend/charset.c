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
#include <stdbool.h>

#include "charset.h"

/*
 * Note: This function performs a lightweight structural check of UTF-8.
 * It only verifies continuation-byte patterns and that the string is
 * properly NUL-terminated.
 *
 * Limitations:
 *  - Overlong encodings are not rejected.
 *  - Surrogate code points (U+D800–U+DFFF) are not rejected.
 *  - Code points above U+10FFFF are not rejected.
 *
 * As a result, this should be treated as a plausibility check for
 * UTF-8 input rather than a fully compliant and security-hardened
 * UTF-8 validator. Callers that need strict UTF-8 validation should
 * perform additional checks at a higher layer.
 */
static bool utf8_is_valid(const char *str)
{
    if (!str)
        return true;

    const unsigned char *bytes = (const unsigned char *)str;
    while (*bytes)
    {
        if (bytes[0] <= 0x7F)
        {
            bytes += 1;
        }
        else if ((bytes[0] & 0xE0) == 0xC0)
        {
            if (bytes[1] == '\0' || (bytes[1] & 0xC0) != 0x80) return false;
            bytes += 2;
        }
        else if ((bytes[0] & 0xF0) == 0xE0)
        {
            if (bytes[1] == '\0' || (bytes[1] & 0xC0) != 0x80 ||
                bytes[2] == '\0' || (bytes[2] & 0xC0) != 0x80) return false;
            bytes += 3;
        }
        else if ((bytes[0] & 0xF8) == 0xF0)
        {
            if (bytes[1] == '\0' || (bytes[1] & 0xC0) != 0x80 ||
                bytes[2] == '\0' || (bytes[2] & 0xC0) != 0x80 ||
                bytes[3] == '\0' || (bytes[3] & 0xC0) != 0x80) return false;
            bytes += 4;
        }
        else
        {
            return false;
        }
    }
    return true;
}

#ifdef _WIN32
char *win32_utf16_to_utf8(const wchar_t *wstr)
{
    if (!wstr)
        return NULL;

    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    if (len <= 0)
        return NULL;

    char *str = malloc((size_t)len);
    if (str)
    {
        WideCharToMultiByte(CP_UTF8, 0, wstr, -1, str, len, NULL, NULL);
    }
    return str;
}

char *utf8_ensure(const char *str)
{
    if (!str)
        return NULL;

    if (utf8_is_valid(str))
    {
        return strdup(str);
    }

    int wn = MultiByteToWideChar(CP_ACP, 0, str, -1, NULL, 0);
    if (wn <= 0)
        return strdup(str);

    wchar_t *ws = malloc((size_t)wn * sizeof(wchar_t));
    if (!ws)
        return strdup(str);

    MultiByteToWideChar(CP_ACP, 0, str, -1, ws, wn);
    char *utf8 = win32_utf16_to_utf8(ws);
    free(ws);

    return utf8 ? utf8 : strdup(str);
}
#else
char *utf8_ensure(const char *str)
{
    if (!str)
        return NULL;

    return strdup(str);
}
#endif
