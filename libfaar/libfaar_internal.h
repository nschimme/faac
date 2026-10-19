#ifndef LIBFAAR_INTERNAL_H
#define LIBFAAR_INTERNAL_H

#include "faar.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <assert.h>

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
#define faar_fft_cbuffer faar_rfftf_cbuffer
#define faar_fft_cforward faar_rfftf_cforward
#define faar_fft_clear faar_rfftf_clear
#else
#define faar_fft_plan faar_rfftd_plan
#define faar_fft_cinit faar_rfftd_cinit
#define faar_fft_cinit_shared faar_rfftd_cinit_shared
#define faar_fft_cbuffer faar_rfftd_cbuffer
#define faar_fft_cforward faar_rfftd_cforward
#define faar_fft_clear faar_rfftd_clear
#endif

#endif /* LIBFAAR_INTERNAL_H */
