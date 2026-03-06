/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2026 Jules (AI Assistant)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#ifndef MXU2_ASM_H
#define MXU2_ASM_H

/*
 * MXU2 instruction macros for MIPS inline assembly using .word encoding.
 * Based on XBurst® Instruction Set Architecture Programming Manual (MXU2).
 */

/* MXU2 Opcodes and Function codes */
#define MXU2_OP_SPECIAL2 0x1c
#define MXU2_OP_COP2     0x12

/* 3RINT funct0 values (from bits [5:0]) */
#define MXU2_3RINT_SUB 0x2e /* Row 101, Col 110 (SUBW) */

/* 3RVEC funct0 values (from bits [5:0]) */
#define MXU2_3RVEC_AND 0x38 /* Row 111, Col 000 (ANDV) */
#define MXU2_3RVEC_XOR 0x3b /* Row 111, Col 011 (XORV) */

/* 3RFP funct0 values (from bits [5:1]) */
#define MXU2_3RFP_FADD 0x00 /* Row 000, Col 00 (FADDW) */
#define MXU2_3RFP_FMUL 0x02 /* Row 000, Col 10 (FMULW) */
#define MXU2_3RFP_FCLT 0x0a /* Row 010, Col 10 (FCLTW) */

/* 2RINT funct0 values (from bits [5:0]) */
#define MXU2_2RINT_MFCPU 0x3e /* Row 111, Col 110 (MFCPUW) */

/* 2RFP funct0 values (from bits [5:1]) */
#define MXU2_2RFP_FSQRT    0x00 /* Row 000, Col 00 (FSQRTW) */
#define MXU2_2RFP_VTRUNCS  0x08 /* Row 010, Col 00 (VTRUNCSWS) */
#define MXU2_2RFP_CFCMXU   0x1e /* Row 111, Col 10 (CFCMXU, bit 0 is fmt) */

/* Register field macros */
#define _MXU2_RS(r)  (((r) & 0x1f) << 21)
#define _MXU2_RT(r)  (((r) & 0x1f) << 16)
#define _MXU2_RD(r)  (((r) & 0x1f) << 11)
#define _MXU2_SA(r)  (((r) & 0x1f) << 6)

/* Instruction generation macros */

/* LU1QX vrd, index(base) : SPECIAL2, rs=base, rt=index, rd=0, sa=vrd, funct=7 */
#define MXU2_LU1QX(vrd, index, base) \
    .word (MXU2_OP_SPECIAL2 << 26) | _MXU2_RS(base) | _MXU2_RT(index) | _MXU2_RD(0) | _MXU2_SA(vrd) | 7

/* SU1QX vrd, index(base) : SPECIAL2, rs=base, rt=index, rd=4, sa=vrd, funct=7 */
#define MXU2_SU1QX(vrd, index, base) \
    .word (MXU2_OP_SPECIAL2 << 26) | _MXU2_RS(base) | _MXU2_RT(index) | _MXU2_RD(4) | _MXU2_SA(vrd) | 7

/* FADDW vrd, vrs, vrt : COP2, rs=24, rt=vrt, rd=vrs, sa=vrd, funct=0 (0*2+0) */
#define MXU2_FADDW(vrd, vrs, vrt) \
    .word (MXU2_OP_COP2 << 26) | _MXU2_RS(24) | _MXU2_RT(vrt) | _MXU2_RD(vrs) | _MXU2_SA(vrd) | 0

/* FMULW vrd, vrs, vrt : COP2, rs=24, rt=vrt, rd=vrs, sa=vrd, funct=4 (2*2+0) */
#define MXU2_FMULW(vrd, vrs, vrt) \
    .word (MXU2_OP_COP2 << 26) | _MXU2_RS(24) | _MXU2_RT(vrt) | _MXU2_RD(vrs) | _MXU2_SA(vrd) | 4

/* FCLTW vrd, vrs, vrt : COP2, rs=24, rt=vrt, rd=vrs, sa=vrd, funct=20 (10*2+0) */
#define MXU2_FCLTW(vrd, vrs, vrt) \
    .word (MXU2_OP_COP2 << 26) | _MXU2_RS(24) | _MXU2_RT(vrt) | _MXU2_RD(vrs) | _MXU2_SA(vrd) | 20

/* FSQRTW vrd, vrs : COP2, rs=30, rt=1, rd=vrs, sa=vrd, funct=0*2+0 = 0 */
#define MXU2_FSQRTW(vrd, vrs) \
    .word (MXU2_OP_COP2 << 26) | _MXU2_RS(30) | _MXU2_RT(1) | _MXU2_RD(vrs) | _MXU2_SA(vrd) | 0

/* VTRUNCSWS vrd, vrs : COP2, rs=30, rt=1, rd=vrs, sa=vrd, funct=10*2+0 = 20 */
#define MXU2_VTRUNCSWS(vrd, vrs) \
    .word (MXU2_OP_COP2 << 26) | _MXU2_RS(30) | _MXU2_RT(1) | _MXU2_RD(vrs) | _MXU2_SA(vrd) | 20

/* ANDV vrd, vrs, vrt : COP2, rs=22, rt=vrt, rd=vrs, sa=vrd, funct=0x38 */
#define MXU2_ANDV(vrd, vrs, vrt) \
    .word (MXU2_OP_COP2 << 26) | _MXU2_RS(22) | _MXU2_RT(vrt) | _MXU2_RD(vrs) | _MXU2_SA(vrd) | 0x38

/* XORV vrd, vrs, vrt : COP2, rs=22, rt=vrt, rd=vrs, sa=vrd, funct=0x3b */
#define MXU2_XORV(vrd, vrs, vrt) \
    .word (MXU2_OP_COP2 << 26) | _MXU2_RS(22) | _MXU2_RT(vrt) | _MXU2_RD(vrs) | _MXU2_SA(vrd) | 0x3b

/* SUBW vrd, vrs, vrt : COP2, rs=17, rt=vrt, rd=vrs, sa=vrd, funct=11*4+2 = 46 */
#define MXU2_SUBW(vrd, vrs, vrt) \
    .word (MXU2_OP_COP2 << 26) | _MXU2_RS(17) | _MXU2_RT(vrt) | _MXU2_RD(vrs) | _MXU2_SA(vrd) | 46

/* MFCPUW vrd, rs : COP2, rs=30, rt=0, rd=rs, sa=vrd, funct=15*4+2 = 62 */
#define MXU2_MFCPUW(vrd, rs) \
    .word (MXU2_OP_COP2 << 26) | _MXU2_RS(30) | _MXU2_RT(0) | _MXU2_RD(rs) | _MXU2_SA(vrd) | 62

/* CFCMXU rd, mcsrs : COP2, rs=30, rt=1, rd=rd, sa=mcsrs, funct=30*2+1 = 61 */
#define MXU2_CFCMXU(rd, mcsrs) \
    .word (MXU2_OP_COP2 << 26) | _MXU2_RS(30) | _MXU2_RT(1) | _MXU2_RD(rd) | _MXU2_SA(mcsrs) | 61

/* MFFPUW vrd, fs : COP2, rs=30, rt=1, rd=fs, sa=vrd, funct=31*2+0 = 62 */
#define MXU2_MFFPUW(vrd, fs) \
    .word (MXU2_OP_COP2 << 26) | _MXU2_RS(30) | _MXU2_RT(1) | _MXU2_RD(fs) | _MXU2_SA(vrd) | 62

/* S32I2M XRa, rb : SPECIAL2, rb in rt(20:16), XRa in rd(10:6), funct=0x2f */
#define MXU_S32I2M(xra, rb) \
    .word (MXU2_OP_SPECIAL2 << 26) | _MXU2_RT(rb) | _MXU2_SA(xra) | 0x0000002f

#endif /* MXU2_ASM_H */
