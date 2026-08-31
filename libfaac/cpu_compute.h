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

#ifndef CPU_COMPUTE_H
#define CPU_COMPUTE_H

/* HAVE_SSE2 is a build-configuration macro: every translation unit that
   dispatches on it must see config.h first, or the SIMD path silently
   compiles out and the scalar fallback is linked instead. */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#if defined(_M_X64) || defined(__x86_64__) || defined(_M_IX86) || defined(__i386__)
# define SSE2_ARCH
#endif

typedef enum {
    CPU_CAP_NONE = 0,
    CPU_CAP_SSE2 = (1 << 0)
} CPUCaps;

CPUCaps get_cpu_caps(void);

#endif
