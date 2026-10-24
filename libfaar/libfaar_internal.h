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

/* Mixed routes High to double, so its float backend needs no compensation. */
#if defined(FAAR_FLOAT) && !defined(FAAR_BACKEND_FLOAT)
#define FAAR_COMPENSATED_FLOAT
#endif

#ifdef FAAR_FLOAT
typedef float faar_sample_t;
#else
typedef double faar_sample_t;
#endif

#include "fft_real.h"
#ifdef FAAR_FLOAT
#define faar_fft_plan faar_rfftf_plan
#define faar_fft_cinit faar_rfftf_cinit
#define faar_fft_cinit_shared faar_rfftf_cinit_shared
#define faar_fft_cinit_shared_single faar_rfftf_cinit_shared_single
#define faar_fft_cbuffer faar_rfftf_cbuffer
#define faar_fft_cforward faar_rfftf_cforward
#define faar_fft_clear faar_rfftf_clear
#define faar_fft_prepare faar_rfftf_prepare
#else
#define faar_fft_plan faar_rfftd_plan
#define faar_fft_cinit faar_rfftd_cinit
#define faar_fft_cinit_shared faar_rfftd_cinit_shared
#define faar_fft_cinit_shared_single faar_rfftd_cinit_shared_single
#define faar_fft_cbuffer faar_rfftd_cbuffer
#define faar_fft_cforward faar_rfftd_cforward
#define faar_fft_clear faar_rfftd_clear
#define faar_fft_prepare faar_rfftd_prepare
#endif

#endif /* LIBFAAR_INTERNAL_H */
