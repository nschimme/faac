/*
 * MXU (MIPS eXtension/enhanced Unit) assembly macros for XBurst1
 * Copyright (C) 2024 Jules
 *
 * This header provides macros to encode MXU instructions using .word
 * for compilers that do not natively support the MXU instruction set.
 */

#ifndef MXU_MACROS_H
#define MXU_MACROS_H

/* MXU Register indices */
#define MXU_XR0  0
#define MXU_XR1  1
#define MXU_XR2  2
#define MXU_XR3  3
#define MXU_XR4  4
#define MXU_XR5  5
#define MXU_XR6  6
#define MXU_XR7  7
#define MXU_XR8  8
#define MXU_XR9  9
#define MXU_XR10 10
#define MXU_XR11 11
#define MXU_XR12 12
#define MXU_XR13 13
#define MXU_XR14 14
#define MXU_XR15 15
#define MXU_XR16 16 /* Alias for MXU_CR */

/* GPR Register numbers (standard MIPS) */
#define MIPS_R0  0
#define MIPS_R1  1
#define MIPS_V0  2
#define MIPS_V1  3
#define MIPS_A0  4
#define MIPS_A1  5
#define MIPS_A2  6
#define MIPS_A3  7
#define MIPS_T0  8
#define MIPS_T1  9
#define MIPS_T2  10
#define MIPS_T3  11
#define MIPS_T4  12
#define MIPS_T5  13
#define MIPS_T6  14
#define MIPS_T7  15
#define MIPS_S0  16
#define MIPS_S1  17
#define MIPS_S2  18
#define MIPS_S3  19
#define MIPS_S4  20
#define MIPS_S5  21
#define MIPS_S6  22
#define MIPS_S7  23
#define MIPS_T8  24
#define MIPS_T9  25
#define MIPS_K0  26
#define MIPS_K1  27
#define MIPS_GP  28
#define MIPS_SP  29
#define MIPS_FP  30
#define MIPS_RA  31

/* MXU Opcodes and Encodings */
#define MXU_MAJOR 0x1C

/* aptn1 */
#define MXU_APTN1_A 0
#define MXU_APTN1_S 1

/* aptn2 / eptn2 */
#define MXU_APTN2_AA 0
#define MXU_APTN2_AS 1
#define MXU_APTN2_SA 2
#define MXU_APTN2_SS 3

/* optn2 */
#define MXU_OPTN2_WW 0
#define MXU_OPTN2_LW 1
#define MXU_OPTN2_HW 2
#define MXU_OPTN2_XW 3

/* Instruction Macros for use in __asm__ blocks */

/* S32I2M XRa, rb: Move from GPR to MXU Register */
#define MXU_S32I2M(xra, rb) \
    ".word (0x1c << 26) | ((" #rb ") << 16) | ((" #xra ") << 6) | 0x2F\n\t"

/* S32I2M_W: Move from GPR to MXU Register (constant encoding) */
#define MXU_S32I2M_W(xra, rb) \
    ((0x1c << 26) | ((rb) << 16) | ((xra) << 6) | 0x2F)

/* S32M2I XRa, rb: Move from MXU Register to GPR */
#define MXU_S32M2I(xra, rb) \
    ".word (0x1c << 26) | ((" #rb ") << 16) | ((" #xra ") << 6) | 0x2E\n\t"

/* S32M2I_W: Move from MXU Register to GPR (constant encoding) */
#define MXU_S32M2I_W(xra, rb) \
    ((0x1c << 26) | ((rb) << 16) | ((xra) << 6) | 0x2E)

/* S32LDD XRa, rb, s12: Load Word with 12-bit offset */
#define MXU_S32LDD(xra, rb, s12) \
    ".word (0x1c << 26) | ((" #rb ") << 21) | ((((" #s12 ") >> 2) & 0x3FF) << 10) | ((" #xra ") << 6) | 0x10\n\t"

/* S32STD XRa, rb, s12: Store Word with 12-bit offset */
#define MXU_S32STD(xra, rb, s12) \
    ".word (0x1c << 26) | ((" #rb ") << 21) | ((((" #s12 ") >> 2) & 0x3FF) << 10) | ((" #xra ") << 6) | 0x11\n\t"

/* D16MUL XRa, XRb, XRc, XRd, optn2: Dual 16-bit Multiply */
#define MXU_D16MUL(xra, xrb, xrc, xrd, optn2) \
    ".word (0x1c << 26) | ((" #optn2 ") << 22) | ((" #xrd ") << 18) | ((" #xrc ") << 14) | ((" #xrb ") << 10) | ((" #xra ") << 6) | 0x08\n\t"

/* D16MUL_W: Dual 16-bit Multiply (constant encoding) */
#define MXU_D16MUL_W(xra, xrb, xrc, xrd, optn2) \
    ((0x1c << 26) | ((optn2) << 22) | ((xrd) << 18) | ((xrc) << 14) | ((xrb) << 10) | ((xra) << 6) | 0x08)

/* Q8MAC XRa, XRb, XRc, XRd, aptn2: Quad 8-bit Multiply-Accumulate */
#define MXU_Q8MAC(xra, xrb, xrc, xrd, aptn2) \
    ".word (0x1c << 26) | ((" #aptn2 ") << 24) | ((" #xrd ") << 18) | ((" #xrc ") << 14) | ((" #xrb ") << 10) | ((" #xra ") << 6) | 0x3A\n\t"

/* Q8MUL XRa, XRb, XRc, XRd: Quad 8-bit Multiply (Unsigned) */
#define MXU_Q8MUL(xra, xrb, xrc, xrd) \
    ".word (0x1c << 26) | ((" #xrd ") << 18) | ((" #xrc ") << 14) | ((" #xrb ") << 10) | ((" #xra ") << 6) | 0x38\n\t"

/* Q8MULSU XRa, XRb, XRc, XRd: Quad 8-bit Multiply (Signed * Unsigned) */
#define MXU_Q8MULSU(xra, xrb, xrc, xrd) \
    ".word (0x1c << 26) | (2 << 22) | ((" #xrd ") << 18) | ((" #xrc ") << 14) | ((" #xrb ") << 10) | ((" #xra ") << 6) | 0x38\n\t"

/* Q8MULSU_W: Quad 8-bit Multiply (constant encoding) */
#define MXU_Q8MULSU_W(xra, xrb, xrc, xrd) \
    ((0x1c << 26) | (2 << 22) | ((xrd) << 18) | ((xrc) << 14) | ((xrb) << 10) | ((xra) << 6) | 0x38)

/* Q8ABD XRa, XRb, XRc: Quad 8-bit Absolute Difference */
#define MXU_Q8ABD(xra, xrb, xrc) \
    ".word (0x1c << 26) | (4 << 18) | ((" #xrc ") << 14) | ((" #xrb ") << 10) | ((" #xra ") << 6) | 0x07\n\t"

/* Q8ABD_W: Quad 8-bit Absolute Difference (constant encoding) */
#define MXU_Q8ABD_W(xra, xrb, xrc) \
    ((0x1c << 26) | (4 << 18) | ((xrc) << 14) | ((xrb) << 10) | ((xra) << 6) | 0x07)

/* D16ASUM XRa, XRb, XRc, XRd, eptn2: Dual 16-bit Accumulate Sum */
#define MXU_D16ASUM(xra, xrb, xrc, xrd, eptn2) \
    ".word (0x1c << 26) | ((" #eptn2 ") << 24) | (2 << 22) | ((" #xrd ") << 18) | ((" #xrc ") << 14) | ((" #xrb ") << 10) | ((" #xra ") << 6) | 0x1B\n\t"

/* D16ASUM_W: Dual 16-bit Accumulate Sum (constant encoding) */
#define MXU_D16ASUM_W(xra, xrb, xrc, xrd, eptn2) \
    ((0x1c << 26) | ((eptn2) << 24) | (2 << 22) | ((xrd) << 18) | ((xrc) << 14) | ((xrb) << 10) | ((xra) << 6) | 0x1B)

/* S16MAD XRa, XRb, XRc, XRd, aptn1, optn2: Single 16-bit Multiply-Add */
#define MXU_S16MAD(xra, xrb, xrc, xrd, aptn1, optn2) \
    ".word (0x1c << 26) | ((" #aptn1 ") << 24) | ((" #optn2 ") << 22) | ((" #xrd ") << 18) | ((" #xrc ") << 14) | ((" #xrb ") << 10) | ((" #xra ") << 6) | 0x0D\n\t"

/* D32ADD XRa, XRb, XRc, XRd, aptn2: Dual 32-bit Add/Sub */
#define MXU_D32ADD(xra, xrb, xrc, xrd, aptn2) \
    ".word (0x1c << 26) | ((" #aptn2 ") << 24) | ((" #xrd ") << 18) | ((" #xrc ") << 14) | ((" #xrb ") << 10) | ((" #xra ") << 6) | 0x18\n\t"

/* Q16ADD XRa, XRb, XRc, XRd, eptn2, optn2: Quad 16-bit Add/Sub */
#define MXU_Q16ADD(xra, xrb, xrc, xrd, eptn2, optn2) \
    ".word (0x1c << 26) | ((" #eptn2 ") << 24) | ((" #optn2 ") << 22) | ((" #xrd ") << 18) | ((" #xrc ") << 14) | ((" #xrb ") << 10) | ((" #xra ") << 6) | 0x0E\n\t"

/* S32AND XRa, XRb, XRc: Bitwise AND */
#define MXU_S32AND(xra, xrb, xrc) \
    ".word (0x1c << 26) | (4 << 18) | ((" #xrc ") << 14) | ((" #xrb ") << 10) | ((" #xra ") << 6) | 0x27\n\t"

/* S32OR XRa, XRb, XRc: Bitwise OR */
#define MXU_S32OR(xra, xrb, xrc) \
    ".word (0x1c << 26) | (5 << 18) | ((" #xrc ") << 14) | ((" #xrb ") << 10) | ((" #xra ") << 6) | 0x27\n\t"

/* S32CPS XRa, XRb, XRc: Copy Sign */
#define MXU_S32CPS(xra, xrb, xrc) \
    ".word (0x1c << 26) | (0 << 14) | ((" #xrc ") << 18) | ((" #xrb ") << 10) | ((" #xra ") << 6) | 0x07\n\t"

/* S32MAX XRa, XRb, XRc: Signed Maximum */
#define MXU_S32MAX(xra, xrb, xrc) \
    ".word (0x1c << 26) | (0 << 18) | ((" #xrc ") << 14) | ((" #xrb ") << 10) | ((" #xra ") << 6) | 0x03\n\t"

/* MXU2 Opcodes and Encodings (Major: COP2=0x12, SPECIAL2=0x1C) */

/* 2R10I: LU1Q vrd, offset(base) -> Op=0x1C, base, offset, vrd, funct0=0x14 */
#define MXU_LU1Q(vrd, base, offset) \
    ".word (0x1c << 26) | ((" #base ") << 21) | (((" #offset ") & 0x3FF) << 11) | ((" #vrd ") << 6) | 0x14\n\t"

/* 2R10I: SU1Q vrd, offset(base) -> Op=0x1C, base, offset, vrd, funct0=0x1C */
#define MXU_SU1Q(vrd, base, offset) \
    ".word (0x1c << 26) | ((" #base ") << 21) | (((" #offset ") & 0x3FF) << 11) | ((" #vrd ") << 6) | 0x1C\n\t"

/* 3RFP: FADDW vrd, vrs, vrt -> Op=0x12, 0x18, vrt, vrs, vrd, 0x0, 0x0 */
#define MXU_FADDW(vrd, vrs, vrt) \
    ".word (0x12 << 26) | (0x18 << 21) | ((" #vrt ") << 16) | ((" #vrs ") << 11) | ((" #vrd ") << 6) | 0x00\n\t"

/* 3RFP: FSUBW vrd, vrs, vrt -> Op=0x12, 0x18, vrt, vrs, vrd, 0x1, 0x0 */
#define MXU_FSUBW(vrd, vrs, vrt) \
    ".word (0x12 << 26) | (0x18 << 21) | ((" #vrt ") << 16) | ((" #vrs ") << 11) | ((" #vrd ") << 6) | 0x01\n\t"

/* 3RFP: FMULW vrd, vrs, vrt -> Op=0x12, 0x18, vrt, vrs, vrd, 0x2, 0x0 */
#define MXU_FMULW(vrd, vrs, vrt) \
    ".word (0x12 << 26) | (0x18 << 21) | ((" #vrt ") << 16) | ((" #vrs ") << 11) | ((" #vrd ") << 6) | 0x02\n\t"

/* 3RFP: FMAXW vrd, vrs, vrt -> Op=0x12, 0x18, vrt, vrs, vrd, 0x0C, 0x0 */
#define MXU_FMAXW(vrd, vrs, vrt) \
    ".word (0x12 << 26) | (0x18 << 21) | ((" #vrt ") << 16) | ((" #vrs ") << 11) | ((" #vrd ") << 6) | (0x0C)\n\t"

/* 2RFP: FSQRTW vrd, vrs -> Op=0x12, 0x1E, 0x1, vrs, vrd, 0x0, 0x0 */
#define MXU_FSQRTW(vrd, vrs) \
    ".word (0x12 << 26) | (0x1E << 21) | ((" #vrs ") << 11) | ((" #vrd ") << 6) | 0x10000\n\t"

/* 2RFP: VTRUNCSWS vrd, vrs -> Op=0x12, 0x1E, 0x1, vrs, vrd, 0x0A, 0x0 */
#define MXU_VTRUNCSWS(vrd, vrs) \
    ".word (0x12 << 26) | (0x1E << 21) | ((" #vrs ") << 11) | ((" #vrd ") << 6) | (0x1000A)\n\t"

/* 3RFP: FCLTW vrd, vrs, vrt -> Op=0x12, 0x18, vrt, vrs, vrd, 0x0A, 0x0 (FCLT.W) */
#define MXU_FCLTW(vrd, vrs, vrt) \
    ".word (0x12 << 26) | (0x1A << 21) | ((" #vrt ") << 16) | ((" #vrs ") << 11) | ((" #vrd ") << 6) | 0x02\n\t"

/* 4R: BSELV vrd, vrs, vrt, vrr -> Op=0x1C, vrr, vrt, vrs, vrd, 0x19 */
#define MXU_BSELV(vrd, vrs, vrt, vrr) \
    ".word (0x1c << 26) | ((" #vrr ") << 21) | ((" #vrt ") << 16) | ((" #vrs ") << 11) | ((" #vrd ") << 6) | 0x19\n\t"

/* 2R8I: REPIW vrd, vrs, imm -> Op=0x1C, 0x2, imm, vrs, vrd, 0x35 */
#define MXU_REPIW(vrd, vrs, imm) \
    ".word (0x1c << 26) | (0x02 << 24) | ((" #imm ") << 16) | ((" #vrs ") << 11) | ((" #vrd ") << 6) | 0x35\n\t"

/* 2R8I: MTCPUSW rd, vrs, imm -> Op=0x1C, 0x2, imm, rd, vrs, 0x33 */
#define MXU_MTCPUSW(rd, vrs, imm) \
    ".word (0x1c << 26) | (0x02 << 24) | ((" #imm ") << 16) | ((" #rd ") << 11) | ((" #vrs ") << 6) | 0x33\n\t"

/* 3RINT: DOTPSH vrd, vrs, vrt -> Op=0x12, 0x12, vrt, vrs, vrd, 0x21 */
#define MXU_DOTPSH(vrd, vrs, vrt) \
    ".word (0x12 << 26) | (0x12 << 21) | ((" #vrt ") << 16) | ((" #vrs ") << 11) | ((" #vrd ") << 6) | 0x21\n\t"

/* 3RINT: DADDSW vrd, vrs, vrt -> Op=0x12, 0x12, vrt, vrs, vrd, 0x26 */
#define MXU_DADDSW(vrd, vrs, vrt) \
    ".word (0x12 << 26) | (0x12 << 21) | ((" #vrt ") << 16) | ((" #vrs ") << 11) | ((" #vrd ") << 6) | 0x26\n\t"

/* 3RVEC: XORV vrd, vrs, vrt -> Op=0x12, 0x16, vrt, vrs, vrd, 0x3B */
#define MXU_XORV(vrd, vrs, vrt) \
    ".word (0x12 << 26) | (0x16 << 21) | ((" #vrt ") << 16) | ((" #vrs ") << 11) | ((" #vrd ") << 6) | 0x3B\n\t"

/* Enable MXU utility */
#define MXU_ENABLE() \
    __asm__ __volatile__ ( \
        "li $2, 1\n\t" \
        MXU_S32I2M(MXU_XR16, 2) \
        "nop\n\t" "nop\n\t" "nop\n\t" \
        : : : "$2" \
    )

#endif /* MXU_MACROS_H */
