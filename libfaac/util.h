/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2001 Menno Bakker
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

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * $Id: util.h,v 1.8 2003/12/20 04:32:48 stux Exp $
 */

#ifndef UTIL_H
#define UTIL_H

#include "faac_real.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <stdlib.h>
#include <memory.h>
#ifdef _WIN32
#include <malloc.h>
#endif

#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif

#ifndef M_PI
#define M_PI        3.14159265358979323846
#endif
#ifndef TWOPI
#define TWOPI       (2.0 * M_PI)
#endif

#if defined(_MSC_VER)
#define ALIGN16_BEG __declspec(align(16))
#define ALIGN16_END
#elif defined(__GNUC__) || defined(__clang__)
#define ALIGN16_BEG
#define ALIGN16_END __attribute__((aligned(16)))
#else
#define ALIGN16_BEG
#define ALIGN16_END
#endif

/* Memory functions */
static inline void *AllocMemory(size_t size) {
    void *ptr;
#if defined(_MSC_VER) || defined(__MINGW32__)
    ptr = _aligned_malloc(size, 16);
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
    ptr = aligned_alloc(16, (size + 15) & ~15);
#else
    if (posix_memalign(&ptr, 16, size) != 0) return NULL;
#endif
    return ptr;
}
static inline void FreeMemory(void *block) {
#if defined(_MSC_VER) || defined(__MINGW32__)
    _aligned_free(block);
#else
    free(block);
#endif
}
#define SetMemory(block, value, size) memset(block, value, size)

int GetSRIndex(unsigned int sampleRate);
unsigned int MaxBitrate(unsigned long sampleRate);
unsigned int MinBitrate();
unsigned int MaxBitresSize(unsigned long bitRate, unsigned long sampleRate);
unsigned int BitAllocation(faac_real pe, int short_block);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* UTIL_H */
