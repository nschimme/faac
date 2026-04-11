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

#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif

/**
 * Portable Count Leading Zeros (CLZ)
 */
#ifdef __GNUC__
#define CLZ(x) __builtin_clz(x)
#else
static inline int CLZ(unsigned int x)
{
    int n = 32;
    if (x == 0) return 32;
    if (x >= 0x10000) { x >>= 16; n -= 16; }
    if (x >= 0x100)   { x >>= 8;  n -= 8;  }
    if (x >= 0x10)    { x >>= 4;  n -= 4;  }
    if (x >= 0x4)     { x >>= 2;  n -= 2;  }
    if (x >= 0x2)     { x >>= 1;  n -= 1;  }
    if (x >= 0x1)     {           n -= 1;  }
    return n;
}
#endif

#ifndef M_PI
#define M_PI        3.14159265358979323846
#endif

/* Memory functions */
#define AllocMemory(size) malloc(size)
#define FreeMemory(block) free(block)
#define SetMemory(block, value, size) memset(block, value, size)

int GetSRIndex(unsigned int sampleRate);
unsigned int MaxBitrate(unsigned long sampleRate);
unsigned int MinBitrate();

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* UTIL_H */
