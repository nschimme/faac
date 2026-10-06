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

#include "dllcheck.h"

#ifdef _WIN32
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _MSC_VER
#include <delayimp.h>
#endif

#ifndef DLL_NAME_DEFAULT
#define DLL_NAME_DEFAULT "library.dll"
#endif

#ifdef _MSC_VER
static FARPROC WINAPI win32_delay_load_hook(unsigned dliNotify, PDelayLoadInfo pdli)
{
    if (dliNotify == dliFailLoadLib)
    {
        const char *dll_name = (pdli && pdli->szDll) ? pdli->szDll : DLL_NAME_DEFAULT;
        fprintf(stderr, "Error: %s was not found. Please ensure %s is in the same directory or system PATH.\n",
                dll_name, dll_name);
        exit(1);
    }
    return NULL;
}

const PfnDliHook __pfnDliFailureHook2 = win32_delay_load_hook;
#endif

bool win32_check_dll_available(const char *dll_name, char *err_msg, size_t err_msg_len)
{
#ifdef FAAC_DYNAMIC_BUILD
    const char *name = dll_name ? dll_name : DLL_NAME_DEFAULT;
    HMODULE hDll = LoadLibraryA(name);
    if (!hDll)
    {
        if (err_msg && err_msg_len > 0)
        {
            snprintf(err_msg, err_msg_len,
                     "Error: %s was not found. Please ensure %s is in the same directory or system PATH.",
                     name, name);
        }
        return false;
    }
    FreeLibrary(hDll);
#else
    (void)dll_name;
    (void)err_msg;
    (void)err_msg_len;
#endif
    return true;
}
#endif
