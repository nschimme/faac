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

#ifdef _MSC_VER /* visual c++ */
#define ALIGN16_BEG __declspec(align(16))
#define ALIGN16_END
#else /* gcc or icc */
#define ALIGN16_BEG
#define ALIGN16_END __attribute__((aligned(16)))
#endif

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <stdlib.h>
#include <memory.h>

#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif

#ifndef M_PI
#define M_PI        3.14159265358979323846
#endif

/* Memory functions */
#define AllocMemory(size) malloc(size)
#define FreeMemory(block) free(block)
#define SetMemory(block, value, size) memset(block, value, size)

typedef enum {
    CPU_CAP_NONE = 0,
    CPU_CAP_SSE2 = 1 << 0,
    CPU_CAP_SSE3 = 1 << 1,
    CPU_CAP_SSSE3 = 1 << 2,
    CPU_CAP_SSE4_1 = 1 << 3,
    CPU_CAP_SSE4_2 = 1 << 4,
    CPU_CAP_AVX = 1 << 5,
    CPU_CAP_AVX2 = 1 << 6,
    CPU_CAP_NEON = 1 << 7,
} cpu_caps_t;

cpu_caps_t get_cpu_caps(void);

int GetSRIndex(unsigned int sampleRate);
unsigned int MaxBitrate(unsigned long sampleRate);
unsigned int MinBitrate();
unsigned int MaxBitresSize(unsigned long bitRate, unsigned long sampleRate);
unsigned int BitAllocation(faac_real pe, int short_block);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* UTIL_H */
