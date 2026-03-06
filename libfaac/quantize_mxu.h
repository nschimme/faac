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
#include <signal.h>
#include <setjmp.h>
#include <sys/prctl.h>
#include <stdio.h>

#ifndef PR_SET_MXU
#define PR_SET_MXU 30
#endif

/*
 * MXU instruction macros for MIPS inline assembly using .word encoding.
 */

/* MXU2 LU1QX vrd, index(base) : SPECIAL2(0x1c) rs=base rt=index rd=0 sa=vrd funct=7 */
#define MXU2_LU1QX(vrd, index, base) \
    ".word (0x1c << 26) | (" #base " << 21) | (" #index " << 16) | (0 << 11) | (" #vrd " << 6) | 7\n\t"

/* MXU2 SU1QX vrd, index(base) : SPECIAL2(0x1c) rs=base rt=index rd=4 sa=vrd funct=7 */
#define MXU2_SU1QX(vrd, index, base) \
    ".word (0x1c << 26) | (" #base " << 21) | (" #index " << 16) | (4 << 11) | (" #vrd " << 6) | 7\n\t"

/* MXU2 3RFP: COP2(0x12) rs=24 vrt vrs vrd funct (fmt=0 for single precision) */
#define MXU2_FADDW(vrd, vrs, vrt) \
    ".word (0x12 << 26) | (24 << 21) | (" #vrt " << 16) | (" #vrs " << 11) | (" #vrd " << 6) | 0\n\t"
#define MXU2_FMULW(vrd, vrs, vrt) \
    ".word (0x12 << 26) | (24 << 21) | (" #vrt " << 16) | (" #vrs " << 11) | (" #vrd " << 6) | 4\n\t"
#define MXU2_FCLTW(vrd, vrs, vrt) \
    ".word (0x12 << 26) | (24 << 21) | (" #vrt " << 16) | (" #vrs " << 11) | (" #vrd " << 6) | 20\n\t"

/* MXU2 2RFP: COP2(0x12) rs=30 rt=1 vrs vrd funct (fmt=0) */
#define MXU2_FSQRTW(vrd, vrs) \
    ".word (0x12 << 26) | (30 << 21) | (1 << 16) | (" #vrs " << 11) | (" #vrd " << 6) | 0\n\t"
#define MXU2_VTRUNCSWS(vrd, vrs) \
    ".word (0x12 << 26) | (30 << 21) | (1 << 16) | (" #vrs " << 11) | (" #vrd " << 6) | 20\n\t"

/* MXU2 3RVEC: COP2(0x12) rs=22 vrt vrs vrd funct */
#define MXU2_ANDV(vrd, vrs, vrt) \
    ".word (0x12 << 26) | (22 << 21) | (" #vrt " << 16) | (" #vrs " << 11) | (" #vrd " << 6) | 56\n\t"
#define MXU2_XORV(vrd, vrs, vrt) \
    ".word (0x12 << 26) | (22 << 21) | (" #vrt " << 16) | (" #vrs " << 11) | (" #vrd " << 6) | 59\n\t"

/* MXU2 3RINT-1: COP2(0x12) rs=17 vrt vrs vrd funct */
#define MXU2_SUBW(vrd, vrs, vrt) \
    ".word (0x12 << 26) | (17 << 21) | (" #vrt " << 16) | (" #vrs " << 11) | (" #vrd " << 6) | 46\n\t"

/* MXU2 2RINT: COP2(0x12) rs=30 rt=0 rs_val vrd funct */
#define MXU2_MFCPUW(vrd, rs_idx) \
    ".word (0x12 << 26) | (30 << 21) | (0 << 16) | (" #rs_idx " << 11) | (" #vrd " << 6) | 62\n\t"

/* MXU2 CFCMXU rd, mcsrs : COP2(0x12) rs=30 rt=1 rd mcsrs 61 */
#define MXU2_CFCMXU(rd, mcsrs) \
    ".word (0x12 << 26) | (30 << 21) | (1 << 16) | (" #rd " << 11) | (" #mcsrs " << 6) | 61\n\t"

/* S32I2M XRa, rb : SPECIAL2(0x1c) 0 rb 0 XRa 0x2f */
#define MXU_S32I2M(xra, rb) \
    ".word (0x1c << 26) | (0 << 21) | (" #rb " << 16) | (0 << 11) | (" #xra " << 6) | 0x2f\n\t"

/* S32M2I rb, XRa : SPECIAL2(0x1c) 0 rb 0 XRa 0x2e */
#define MXU_S32M2I(rb, xra) \
    ".word (0x1c << 26) | (0 << 21) | (" #rb " << 16) | (0 << 11) | (" #xra " << 6) | 0x2e\n\t"

#if defined(__mips__)
static sigjmp_buf mxu_jmpbuf;
static void mxu_sigill_handler(int sig)
{
    siglongjmp(mxu_jmpbuf, 1);
}

static inline int check_mxu1_support(void)
{
    struct sigaction sa, old_sa;
    int supported = 0;

    sa.sa_handler = mxu_sigill_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGILL, &sa, &old_sa) == 0) {
        if (sigsetjmp(mxu_jmpbuf, 1) == 0) {
            prctl(PR_SET_MXU, 1, 0, 0, 0);
            prctl(31, 1, 0, 0, 0);

            int val = 1;
            __asm__ __volatile__ (
                "move $t0, %0\n\t"
                MXU_S32I2M(16, 8) // XR16 = 1
                "nop; nop; nop\n\t"
                ".word 0x70000010\n\t" // S32LDD XR0, $zero, 0
                : : "r"(val) : "$t0"
            );
            supported = 1;
        }
        sigaction(SIGILL, &old_sa, NULL);
    }
    return supported;
}

static inline int check_mxu2_support(void)
{
    struct sigaction sa, old_sa;
    int supported = 0;

    sa.sa_handler = mxu_sigill_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGILL, &sa, &old_sa) == 0) {
        if (sigsetjmp(mxu_jmpbuf, 1) == 0) {
            prctl(PR_SET_MXU, 1, 0, 0, 0);
            prctl(31, 1, 0, 0, 0);

            int val = 3;
            __asm__ __volatile__ (
                "move $t0, %0\n\t"
                MXU_S32I2M(16, 8)
                "nop; nop; nop\n\t"
                : : "r"(val) : "$t0"
            );

            int mir = 0;
            __asm__ __volatile__ (
                "li $t0, 0xdead\n\t"
                MXU2_CFCMXU(8, 0)
                "move %0, $t0\n\t"
                : "=r"(mir) : : "$t0"
            );
            if (mir != 0xdead && mir != 0)
                supported = 1;
        }
        sigaction(SIGILL, &old_sa, NULL);
    }
    return supported;
}
#endif

#endif /* QUANTIZE_MXU_H */
