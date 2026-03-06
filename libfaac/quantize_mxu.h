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
#include <errno.h>
#include <string.h>

#ifndef PR_SET_MXU
#define PR_SET_MXU 30
#endif

/*
 * MXU instruction macros for MIPS inline assembly using .word encoding.
 * Registers must be passed as literal numbers ($t0=8, $t1=9, etc.)
 */

/* MXU1 Instructions (SPECIAL2 R-type) */
/* S32I2M XRa, rb : Major(28) rs(0) rt(rb) rd(0) sa(xra) funct(47) */
#define MXU_S32I2M(xra, rb) \
    ".word (28 << 26) | (0 << 21) | (" #rb " << 16) | (0 << 11) | (" #xra " << 6) | 47\n\t"

/* S32M2I rb, XRa : Major(28) rs(0) rt(rb) rd(0) sa(xra) funct(46) */
#define MXU_S32M2I(rb, xra) \
    ".word (28 << 26) | (0 << 21) | (" #rb " << 16) | (0 << 11) | (" #xra " << 6) | 46\n\t"

/* S32CPS XRa, XRb, XRc : Major(28) rs(0) rt(XRc) rd(XRb) sa(XRa) funct(7) */
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
/* VTRUNCSWS: Page 113 says funct=10 (01010). fmt=0. Bits 5:0 = 10100 = 20. */
#define MXU2_VTRUNCSWS(vrd, vrs) \
    ".word (18 << 26) | (30 << 21) | (1 << 16) | (" #vrs " << 11) | (" #vrd " << 6) | 20\n\t"

/* 3RVEC: COP2(18) rs=22 rt=vrt rd=vrs sa=vrd funct(0x38=56) */
#define MXU2_ANDV(vrd, vrs, vrt) \
    ".word (18 << 26) | (22 << 21) | (" #vrt " << 16) | (" #vrs " << 11) | (" #vrd " << 6) | 56\n\t"
#define MXU2_XORV(vrd, vrs, vrt) \
    ".word (18 << 26) | (22 << 21) | (" #vrt " << 16) | (" #vrs " << 11) | (" #vrd " << 6) | 59\n\t"

/* 3RINT-1: COP2(18) rs=17 rt=vrt rd=vrs sa=vrd funct(46) */
#define MXU2_SUBW(vrd, vrs, vrt) \
    ".word (18 << 26) | (17 << 21) | (" #vrt " << 16) | (" #vrs " << 11) | (" #vrd " << 6) | 46\n\t"

/* 2RINT: COP2(18) rs=30 rt=0 rd=rs_idx sa=vrd funct(62) */
#define MXU2_MFCPUW(vrd, rs_val) \
    ".word (18 << 26) | (30 << 21) | (0 << 16) | (" #rs_val " << 11) | (" #vrd " << 6) | 62\n\t"

/* CFCMXU rd, mcsrs : COP2(18) rs=30 rt=1 rd=rd sa=mcsrs funct=30 fmt=1 (Bits 5:0 = 61) */
#define MXU2_CFCMXU(rd, mcsrs) \
    ".word (18 << 26) | (30 << 21) | (1 << 16) | (" #rd " << 11) | (" #mcsrs " << 6) | 61\n\t"


#if defined(__mips__)
static sigjmp_buf mxu_jmpbuf;
static void mxu_crash_handler(int sig)
{
    siglongjmp(mxu_jmpbuf, 1);
}

static inline void enable_mxu_kernel(void)
{
    /* Try common Ingenic prctl codes to enable MXU/MXU2 */
    prctl(PR_SET_MXU, 1, 0, 0, 0);
    prctl(31, 1, 0, 0, 0);
}

static inline int check_mxu1_support(void)
{
    struct sigaction sa, old_sa_ill, old_sa_bus, old_sa_segv;
    int supported = 0;

    sa.sa_handler = mxu_crash_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGILL, &sa, &old_sa_ill);
    sigaction(SIGBUS, &sa, &old_sa_bus);
    sigaction(SIGSEGV, &sa, &old_sa_segv);

    if (sigsetjmp(mxu_jmpbuf, 1) == 0) {
        enable_mxu_kernel();

        /* Attempt to enable MXU engine via XR16 (setting bit 0) */
        int enable_val = 1;
        __asm__ __volatile__ (
            "move $t0, %0\n\t"
            MXU_S32I2M(16, 8) // XR16 = $t0
            "nop; nop; nop\n\t"
            : : "r"(enable_val) : "$t0"
        );

        /* Verify by reading XR0 (should always be 0) */
        int val = 0xdead;
        __asm__ __volatile__ (
            "li $t0, 0xdead\n\t"
            MXU_S32M2I(8, 0) // $t0 = XR0
            "move %0, $t0\n\t"
            : "=r"(val) : : "$t0"
        );
        if (val == 0) supported = 1;
    }

    sigaction(SIGILL, &old_sa_ill, NULL);
    sigaction(SIGBUS, &old_sa_bus, NULL);
    sigaction(SIGSEGV, &old_sa_segv, NULL);

    return supported;
}

static inline int check_mxu2_support(void)
{
    struct sigaction sa, old_sa_ill, old_sa_bus, old_sa_segv;
    int supported = 0;

    sa.sa_handler = mxu_crash_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGILL, &sa, &old_sa_ill);
    sigaction(SIGBUS, &sa, &old_sa_bus);
    sigaction(SIGSEGV, &sa, &old_sa_segv);

    if (sigsetjmp(mxu_jmpbuf, 1) == 0) {
        enable_mxu_kernel();

        /* Attempt to enable MXU2 engine via XR16 (setting bit 1 as well) */
        int enable_val = 3;
        __asm__ __volatile__ (
            "move $t0, %0\n\t"
            MXU_S32I2M(16, 8) // XR16 = $t0
            "nop; nop; nop\n\t"
            : : "r"(enable_val) : "$t0"
        );

        /* Attempt to read MIR register. */
        int mir = 0;
        __asm__ __volatile__ (
            "li $t0, 0\n\t"
            MXU2_CFCMXU(8, 0) // Read MIR (reg 0) into $t0
            "move %0, $t0\n\t"
            : "=r"(mir) : : "$t0"
        );
        if (mir != 0) supported = 1;
    }

    sigaction(SIGILL, &old_sa_ill, NULL);
    sigaction(SIGBUS, &old_sa_bus, NULL);
    sigaction(SIGSEGV, &old_sa_segv, NULL);

    return supported;
}

static inline unsigned int get_mips_prid(void)
{
    unsigned int prid = 0;
    struct sigaction sa, old_sa_ill, old_sa_bus, old_sa_segv;
    sa.sa_handler = mxu_crash_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGILL, &sa, &old_sa_ill);
    sigaction(SIGBUS, &sa, &old_sa_bus);
    sigaction(SIGSEGV, &sa, &old_sa_segv);

    if (sigsetjmp(mxu_jmpbuf, 1) == 0) {
        __asm__ __volatile__ ("mfc0 %0, $15, 0" : "=r"(prid));
    }

    sigaction(SIGILL, &old_sa_ill, NULL);
    sigaction(SIGBUS, &old_sa_bus, NULL);
    sigaction(SIGSEGV, &old_sa_segv, NULL);
    return prid;
}
#endif

#endif /* QUANTIZE_MXU_H */
