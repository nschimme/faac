/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2026 Jules (AI Assistant)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#ifndef QUANTIZE_MXU_H
#define QUANTIZE_MXU_H

#include <stddef.h>
#include "faac_real.h"

/*
 * We include local copies of Ingenic headers to ensure bit-correctness
 * of macros while maintaining portability for non-Ingenic environments.
 */
#if defined(__mips__)
#include "mxu_media.h"

/*
 * Map Ingenic macros to our project's SIMD interface.
 * T31 core uses MXU2 for SIMD Floating Point.
 */

/* MXU2 SIMD Floating Point (using documented opcodes since mxu_media.h is integer-focused) */
#define MXU2_LU1QX_BITFIELD(vrd, index, base) \
    ".word (28 << 26) | (" #base " << 21) | (" #index " << 16) | (0 << 11) | (" #vrd " << 6) | 7\n\t"
#define MXU2_SU1QX_BITFIELD(vrd, index, base) \
    ".word (28 << 26) | (" #base " << 21) | (" #index " << 16) | (4 << 11) | (" #vrd " << 6) | 7\n\t"

/* Arithmetic */
#define MXU2_FADDW_BITFIELD(vrd, vrs, vrt) \
    ".word (18 << 26) | (24 << 21) | (" #vrt " << 16) | (" #vrs " << 11) | (" #vrd " << 6) | 0\n\t"
#define MXU2_FMULW_BITFIELD(vrd, vrs, vrt) \
    ".word (18 << 26) | (24 << 21) | (" #vrt " << 16) | (" #vrs " << 11) | (" #vrd " << 6) | 4\n\t"
#define MXU2_FCLTW_BITFIELD(vrd, vrs, vrt) \
    ".word (18 << 26) | (24 << 21) | (" #vrt " << 16) | (" #vrs " << 11) | (" #vrd " << 6) | 20\n\t"
#define MXU2_FSQRTW_BITFIELD(vrd, vrs) \
    ".word (18 << 26) | (30 << 21) | (1 << 16) | (" #vrs " << 11) | (" #vrd " << 6) | 0\n\t"
#define MXU2_VTRUNCSWS_BITFIELD(vrd, vrs) \
    ".word (18 << 26) | (30 << 21) | (1 << 16) | (" #vrs " << 11) | (" #vrd " << 6) | 20\n\t"

/* Logical */
#define MXU2_ANDV_BITFIELD(vrd, vrs, vrt) \
    ".word (18 << 26) | (22 << 21) | (" #vrt " << 16) | (" #vrs " << 11) | (" #vrd " << 6) | 56\n\t"
#define MXU2_XORV_BITFIELD(vrd, vrs, vrt) \
    ".word (18 << 26) | (22 << 21) | (" #vrt " << 16) | (" #vrs " << 11) | (" #vrd " << 6) | 59\n\t"

/* Integer fallback for sign apply */
#define MXU2_SUBW_BITFIELD(vrd, vrs, vrt) \
    ".word (18 << 26) | (17 << 21) | (" #vrt " << 16) | (" #vrs " << 11) | (" #vrd " << 6) | 46\n\t"

/* Transfers */
#define MXU2_MFCPUW_BITFIELD(vrd, rs_idx) \
    ".word (18 << 26) | (30 << 21) | (0 << 16) | (" #rs_idx " << 11) | (" #vrd " << 6) | 62\n\t"

void QuantizeInitMXU(void);
void quantize_mxu1(const faac_real * __restrict xr, int * __restrict xi, int n, faac_real sfacfix);
void quantize_mxu2(const faac_real * __restrict xr, int * __restrict xi, int n, faac_real sfacfix);

int check_mxu1_support(void);
int check_mxu2_support(void);
void get_cpu_info(char *buf, size_t len);
unsigned int get_mips_prid(void);
#endif

#endif /* QUANTIZE_MXU_H */
