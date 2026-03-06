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
 * MXU instruction macros for MIPS inline assembly using .word encoding.
 */

/* MXU1 Instructions (SPECIAL2 R-type, Major=28=0x1C) */
/* S32I2M XRa, rb : Major(28) rs(0) rt(rb) rd(0) sa(xra) funct(47) */
#define MXU_S32I2M(xra, rb) \
    ".word (28 << 26) | (0 << 21) | (" #rb " << 16) | (0 << 11) | (" #xra " << 6) | 47\n\t"

/* S32M2I rb, XRa : Major(28) rs(0) rt(rb) rd(0) sa(xra) funct(46) */
#define MXU_S32M2I(rb, xra) \
    ".word (28 << 26) | (0 << 21) | (" #rb " << 16) | (0 << 11) | (" #xra " << 6) | 46\n\t"

/* S32CPS XRa, XRb, XRc : Major(28) rs(0) rt(xrc) rd(xrb) sa(xra) funct(7) */
#define MXU_S32CPS(xra, xrb, xrc) \
    ".word (28 << 26) | (0 << 21) | (" #xrc " << 16) | (" #xrb " << 11) | (" #xra " << 6) | 7\n\t"


/* MXU2 Instructions (COP2/SPECIAL2 R-type) */
/* LU1QX vrd, index(base) : Major(28) rs=base rt=index rd=0 sa=vrd funct=7 */
#define MXU2_LU1QX(vrd, index, base) \
    ".word (28 << 26) | (" #base " << 21) | (" #index " << 16) | (0 << 11) | (" #vrd " << 6) | 7\n\t"

/* SU1QX vrd, index(base) : Major(28) rs=base rt=index rd=4 sa=vrd funct=7 */
#define MXU2_SU1QX(vrd, index, base) \
    ".word (28 << 26) | (" #base " << 21) | (" #index " << 16) | (4 << 11) | (" #vrd " << 6) | 7\n\t"

/* 3RFP: COP2(18) rs=24 rt=vrt rd=vrs sa=vrd funct (fmt=0) */
#define MXU2_FADDW(vrd, vrs, vrt) \
    ".word (18 << 26) | (24 << 21) | (" #vrt " << 16) | (" #vrs " << 11) | (" #vrd " << 6) | 0\n\t"
#define MXU2_FMULW(vrd, vrs, vrt) \
    ".word (18 << 26) | (24 << 21) | (" #vrt " << 16) | (" #vrs " << 11) | (" #vrd " << 6) | 4\n\t"
#define MXU2_FCLTW(vrd, vrs, vrt) \
    ".word (18 << 26) | (24 << 21) | (" #vrt " << 16) | (" #vrs " << 11) | (" #vrd " << 6) | 20\n\t"

/* 2RFP: COP2(18) rs=30 rt=1 rd=vrs sa=vrd funct (fmt=0) */
#define MXU2_FSQRTW(vrd, vrs) \
    ".word (18 << 26) | (30 << 21) | (1 << 16) | (" #vrs " << 11) | (" #vrd " << 6) | 0\n\t"
#define MXU2_VTRUNCSWS(vrd, vrs) \
    ".word (18 << 26) | (30 << 21) | (1 << 16) | (" #vrs " << 11) | (" #vrd " << 6) | 20\n\t"

/* 3RVEC: COP2(18) rs=22 rt=vrt rd=vrs sa=vrd funct(56) */
#define MXU2_ANDV(vrd, vrs, vrt) \
    ".word (18 << 26) | (22 << 21) | (" #vrt " << 16) | (" #vrs " << 11) | (" #vrd " << 6) | 56\n\t"
#define MXU2_XORV(vrd, vrs, vrt) \
    ".word (18 << 26) | (22 << 21) | (" #vrt " << 16) | (" #vrs " << 11) | (" #vrd " << 6) | 59\n\t"

/* 3RINT-1: COP2(18) rs=17 rt=vrt rd=vrs sa=vrd funct(46) */
#define MXU2_SUBW(vrd, vrs, vrt) \
    ".word (18 << 26) | (17 << 21) | (" #vrt " << 16) | (" #vrs " << 11) | (" #vrd " << 6) | 46\n\t"

/* 2RINT: COP2(18) rs=30 rt=0 rd=rs_idx sa=vrd funct(62) */
#define MXU2_MFCPUW(vrd, rs_idx) \
    ".word (18 << 26) | (30 << 21) | (0 << 16) | (" #rs_idx " << 11) | (" #vrd " << 6) | 62\n\t"

/* CFCMXU rd, mcsrs : COP2(18) rs=30 rt=1 rd=rd sa=mcsrs funct=61 */
#define MXU2_CFCMXU(rd, mcsrs) \
    ".word (18 << 26) | (30 << 21) | (1 << 16) | (" #rd " << 11) | (" #mcsrs " << 6) | 61\n\t"

#if defined(__mips__)
void QuantizeInitMXU(void);
void quantize_mxu1(const faac_real * __restrict xr, int * __restrict xi, int n, faac_real sfacfix);
void quantize_mxu2(const faac_real * __restrict xr, int * __restrict xi, int n, faac_real sfacfix);

int check_mxu1_support(void);
int check_mxu2_support(void);
void get_cpu_info(char *buf, size_t len);
unsigned int get_mips_prid(void);
#endif

#endif /* QUANTIZE_MXU_H */
