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

/* S32M2I XRa, rb: Move from MXU Register to GPR */
#define MXU_S32M2I(xra, rb) \
    ".word (0x1c << 26) | ((" #rb ") << 16) | ((" #xra ") << 6) | 0x2E\n\t"

/* S32LDD XRa, rb, s12: Load Word with 12-bit offset */
#define MXU_S32LDD(xra, rb, s12) \
    ".word (0x1c << 26) | ((" #rb ") << 21) | ((((" #s12 ") >> 2) & 0x3FF) << 10) | ((" #xra ") << 6) | 0x10\n\t"

/* S32STD XRa, rb, s12: Store Word with 12-bit offset */
#define MXU_S32STD(xra, rb, s12) \
    ".word (0x1c << 26) | ((" #rb ") << 21) | ((((" #s12 ") >> 2) & 0x3FF) << 10) | ((" #xra ") << 6) | 0x11\n\t"

/* D16MUL XRa, XRb, XRc, XRd, optn2: Dual 16-bit Multiply */
#define MXU_D16MUL(xra, xrb, xrc, xrd, optn2) \
    ".word (0x1c << 26) | ((" #optn2 ") << 22) | ((" #xrd ") << 18) | ((" #xrc ") << 14) | ((" #xrb ") << 10) | ((" #xra ") << 6) | 0x08\n\t"

/* Q8MAC XRa, XRb, XRc, XRd, aptn2: Quad 8-bit Multiply-Accumulate */
#define MXU_Q8MAC(xra, xrb, xrc, xrd, aptn2) \
    ".word (0x1c << 26) | ((" #aptn2 ") << 24) | ((" #xrd ") << 18) | ((" #xrc ") << 14) | ((" #xrb ") << 10) | ((" #xra ") << 6) | 0x3A\n\t"

/* Q8MUL XRa, XRb, XRc, XRd: Quad 8-bit Multiply (Unsigned) */
#define MXU_Q8MUL(xra, xrb, xrc, xrd) \
    ".word (0x1c << 26) | ((" #xrd ") << 18) | ((" #xrc ") << 14) | ((" #xrb ") << 10) | ((" #xra ") << 6) | 0x38\n\t"

/* Q8MULSU XRa, XRb, XRc, XRd: Quad 8-bit Multiply (Signed * Unsigned) */
#define MXU_Q8MULSU(xra, xrb, xrc, xrd) \
    ".word (0x1c << 26) | (2 << 22) | ((" #xrd ") << 18) | ((" #xrc ") << 14) | ((" #xrb ") << 10) | ((" #xra ") << 6) | 0x38\n\t"

/* D16ASUM XRa, XRb, XRc, XRd, eptn2: Dual 16-bit Accumulate Sum */
#define MXU_D16ASUM(xra, xrb, xrc, xrd, eptn2) \
    ".word (0x1c << 26) | ((" #eptn2 ") << 24) | (2 << 22) | ((" #xrd ") << 18) | ((" #xrc ") << 14) | ((" #xrb ") << 10) | ((" #xra ") << 6) | 0x1B\n\t"

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
    ".word (0x1c << 26) | (4 << 21) | ((" #xrc ") << 14) | ((" #xrb ") << 10) | ((" #xra ") << 6) | 0x27\n\t"

/* S32OR XRa, XRb, XRc: Bitwise OR */
#define MXU_S32OR(xra, xrb, xrc) \
    ".word (0x1c << 26) | (5 << 21) | ((" #xrc ") << 14) | ((" #xrb ") << 10) | ((" #xra ") << 6) | 0x27\n\t"

/* S32CPS XRa, XRb, XRc: Copy Sign */
#define MXU_S32CPS(xra, xrb, xrc) \
    ".word (0x1c << 26) | ((" #xrc ") << 14) | ((" #xrb ") << 10) | ((" #xra ") << 6) | 0x07\n\t"

/* S32MAX XRa, XRb, XRc: Signed Maximum */
#define MXU_S32MAX(xra, xrb, xrc) \
    ".word (0x1c << 26) | (0 << 21) | (0 << 18) | ((" #xrc ") << 14) | ((" #xrb ") << 10) | ((" #xra ") << 6) | 0x03\n\t"

/* Q8ABD XRa, XRb, XRc: Quad 8-bit Absolute Difference */
#define MXU_Q8ABD(xra, xrb, xrc) \
    ".word (0x1c << 26) | (0 << 21) | (4 << 18) | ((" #xrc ") << 14) | ((" #xrb ") << 10) | ((" #xra ") << 6) | 0x07\n\t"

/* Enable MXU utility */
#define MXU_ENABLE() \
    __asm__ __volatile__ ( \
        "li $v0, 1\n\t" \
        MXU_S32I2M(MXU_XR16, MIPS_V0) \
        "nop\n\t" "nop\n\t" "nop\n\t" \
        : : : "v0" \
    )

#endif /* MXU_MACROS_H */
