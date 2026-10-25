/*
 * FAAR - Freeware Advanced Audio Resampler
 * Copyright (C) 2026 Nils Schimmelmann
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 */
#ifndef LIBFAAR_INTERNAL_H
#define LIBFAAR_INTERNAL_H

#include "faar.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <assert.h>

/* Ports override both hooks with -D or a force-included header. Detect the
 * overrides first: libc realloc must never see a custom allocator's block. */
#if defined(AllocMemory) != defined(FreeMemory)
#error "Override AllocMemory and FreeMemory together"
#endif
#if defined(AllocMemory)
#define FAAR_CUSTOM_ALLOCATOR 1
#else
#define AllocMemory(size) malloc(size)
#define FreeMemory(block) free(block)
#endif

/* Zeroed array allocation; AllocMemory need not clear, and calloc's overflow
 * check must survive the conversion. */
static inline void *faar_alloc_zeroed(size_t count, size_t size)
{
    void *block;
    if (size && count > SIZE_MAX / size) return NULL;
    block = AllocMemory(count * size);
    if (block) memset(block, 0, count * size);
    return block;
}

/* Grow a block, preserving only initialized bytes. On failure the old block
 * remains owned by the caller. Standard builds retain in-place libc growth. */
static inline void *faar_grow_memory(void *block, size_t used, size_t bytes)
{
#ifdef FAAR_CUSTOM_ALLOCATOR
    void *grown = AllocMemory(bytes);
    if (!grown) return NULL;
    if (used) memcpy(grown, block, used);
    FreeMemory(block);
    return grown;
#else
    (void)used;
    return realloc(block, bytes);
#endif
}

/* High's sensitive kernels need more than float precision. Float builds
 * emulate it with float2 so targets without a double FPU still run High;
 * mixed runs the same kernels in native double, which is ten times faster. */
#ifndef FAAR_WIDE_HIGH
#define FAAR_COMPENSATED_FLOAT
#endif

/* Sample planes; distinct from the API's float so storage stays greppable. */
typedef float faar_sample_t;

#include "fft_real.h"
#define faar_fft_plan faar_rfftf_plan
#define faar_fft_cinit faar_rfftf_cinit
#define faar_fft_cinit_shared faar_rfftf_cinit_shared
#define faar_fft_cinit_shared_single faar_rfftf_cinit_shared_single
#define faar_fft_cinit_filter_shared faar_rfftf_cinit_filter_shared
#define faar_fft_cbuffer faar_rfftf_cbuffer
#define faar_fft_cforward faar_rfftf_cforward
#define faar_fft_clear faar_rfftf_clear
#define faar_fft_prepare faar_rfftf_prepare

#endif /* LIBFAAR_INTERNAL_H */
