/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2026 Jules (AI Assistant)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <setjmp.h>
#include <sys/prctl.h>
#include <errno.h>
#include <unistd.h>
#include "faac_real.h"
#include "quantize.h"
#include "quantize_mxu.h"

#ifndef FAAC_PRECISION_SINGLE
#error MXU implementation only supports single precision floats
#endif

#ifndef PR_SET_MXU
#define PR_SET_MXU 30
#endif

/* MXU2 SIMD Floating Point Macros for T31 (Hand-encoded) */
#define MXU2_LU1QX(vrd, idx, base) \
    ".word (28 << 26) | (" #base " << 21) | (" #idx " << 16) | (0 << 11) | (" #vrd " << 6) | 7\n\t"
#define MXU2_SU1QX(vrd, idx, base) \
    ".word (28 << 26) | (" #base " << 21) | (" #idx " << 16) | (4 << 11) | (" #vrd " << 6) | 7\n\t"
#define MXU2_FADDW(vrd, vrs, vrt) \
    ".word (18 << 26) | (24 << 21) | (" #vrt " << 16) | (" #vrs " << 11) | (" #vrd " << 6) | 0\n\t"
#define MXU2_FMULW(vrd, vrs, vrt) \
    ".word (18 << 26) | (24 << 21) | (" #vrt " << 16) | (" #vrs " << 11) | (" #vrd " << 6) | 4\n\t"
#define MXU2_FCLTW(vrd, vrs, vrt) \
    ".word (18 << 26) | (24 << 21) | (" #vrt " << 16) | (" #vrs " << 11) | (" #vrd " << 6) | 20\n\t"
#define MXU2_FSQRTW(vrd, vrs) \
    ".word (18 << 26) | (30 << 21) | (1 << 16) | (" #vrs " << 11) | (" #vrd " << 6) | 0\n\t"
#define MXU2_VTRUNCSWS(vrd, vrs) \
    ".word (18 << 26) | (30 << 21) | (1 << 16) | (" #vrs " << 11) | (" #vrd " << 6) | 20\n\t"
#define MXU2_ANDV(vrd, vrs, vrt) \
    ".word (18 << 26) | (22 << 21) | (" #vrt " << 16) | (" #vrs " << 11) | (" #vrd " << 6) | 56\n\t"
#define MXU2_XORV(vrd, vrs, vrt) \
    ".word (18 << 26) | (22 << 21) | (" #vrt " << 16) | (" #vrs " << 11) | (" #vrd " << 6) | 59\n\t"
#define MXU2_SUBW(vrd, vrs, vrt) \
    ".word (18 << 26) | (17 << 21) | (" #vrt " << 16) | (" #vrs " << 11) | (" #vrd " << 6) | 46\n\t"
#define MXU2_MFCPUW(vrd, rs_idx) \
    ".word (18 << 26) | (30 << 21) | (0 << 16) | (" #rs_idx " << 11) | (" #vrd " << 6) | 62\n\t"

static float pow075_lut[4096];
static int lut_initialized = 0;

void QuantizeInitMXU(void)
{
    if (lut_initialized) return;
    int i;
    for (i = 0; i < 4096; i++) {
        float y = (float)i / 1024.0f;
        pow075_lut[i] = powf(y, 0.75f);
    }
    lut_initialized = 1;
}

#if defined(__mips__)
static sigjmp_buf mxu_jmpbuf;
static void mxu_crash_handler(int sig) { siglongjmp(mxu_jmpbuf, 1); }

static void enable_mxu_engine(void)
{
    int r1 = prctl(30, 1, 0, 0, 0); // PR_SET_MXU
    int r2 = prctl(31, 1, 0, 0, 0);
    fprintf(stderr, "MXU: prctl(30,1)=%d, prctl(31,1)=%d, errno=%d\n", r1, r2, errno);
    __asm__ __volatile__ (
        ".set push\n\t"
        ".set noat\n\t"
        "li $1, 3\n\t"
        ".word 0x7001042f\n\t" // S32I2M XR16, $1
        "nop; nop; nop; nop; nop\n\t"
        "sync\n\t"
        ".set pop\n\t"
        : : : "$1", "memory"
    );
}

int check_mxu1_support(void)
{
    struct sigaction sa, osa;
    int supported = 0;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = mxu_crash_handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGILL, &sa, &osa) == 0) {
        if (sigsetjmp(mxu_jmpbuf, 1) == 0) {
            enable_mxu_engine();
            int rv0 = 0xbeef;
            __asm__ __volatile__ (
                ".set push\n\t"
                ".set noat\n\t"
                "li $1, 0xdead\n\t"
                ".word 0x7001002e\n\t" // S32M2I $1, XR0
                "move %0, $1\n\t"
                ".set pop\n\t"
                : "=r"(rv0) : : "$1"
            );
            if (rv0 == 0) supported = 1;
        }
        sigaction(SIGILL, &osa, NULL);
    }
    fprintf(stderr, "MXU: Probe MXU1 result: %d\n", supported);
    return supported;
}

int check_mxu2_support(void)
{
    struct sigaction sa, osa;
    int supported = 0;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = mxu_crash_handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGILL, &sa, &osa) == 0) {
        if (sigsetjmp(mxu_jmpbuf, 1) == 0) {
            enable_mxu_engine();
            int mir = 0;
            __asm__ __volatile__ (
                ".set push\n\t"
                ".set noat\n\t"
                "li $1, 0\n\t"
                ".word 0x4bc1083d\n\t" // CFCMXU $1, MIR
                "move %0, $1\n\t"
                ".set pop\n\t"
                : "=r"(mir) : : "$1"
            );
            if (mir != 0) supported = 1;
        }
        sigaction(SIGILL, &osa, NULL);
    }
    fprintf(stderr, "MXU: Probe MXU2 result: %d\n", supported);
    return supported;
}

void get_cpu_info(char *buf, size_t len)
{
    FILE *f = fopen("/proc/cpuinfo", "r");
    buf[0] = '\0';
    if (!f) { strncpy(buf, "Error opening /proc/cpuinfo", len); return; }
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "cpu model") || strstr(line, "ASEs implemented") || strstr(line, "processor")) {
            strncat(buf, line, len - strlen(buf) - 1);
        }
    }
    fclose(f);
}

unsigned int get_mips_prid(void)
{
    unsigned int prid = 0;
    struct sigaction sa, osa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = mxu_crash_handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGILL, &sa, &osa) == 0) {
        if (sigsetjmp(mxu_jmpbuf, 1) == 0) {
            /* Try PRID read */
            __asm__ __volatile__ ("mfc0 %0, $15, 0" : "=r"(prid));
        }
        sigaction(SIGILL, &osa, NULL);
    }
    return prid;
}
#endif

void quantize_mxu1(const faac_real * __restrict xr, int * __restrict xi, int n, faac_real sfacfix)
{
    int cnt = 0;
    const float magic = (float)MAGIC_NUMBER;
    if (!lut_initialized) QuantizeInitMXU();
    for (; cnt <= n - 4; cnt += 4) {
        float v0 = xr[cnt], v1 = xr[cnt+1], v2 = xr[cnt+2], v3 = xr[cnt+3];
        float y0 = (v0 < 0 ? -v0 : v0) * sfacfix;
        float y1 = (v1 < 0 ? -v1 : v1) * sfacfix;
        float y2 = (v2 < 0 ? -v2 : v2) * sfacfix;
        float y3 = (v3 < 0 ? -v3 : v3) * sfacfix;
        float r0, r1, r2, r3;
        if (y0 < 4.0f) r0 = pow075_lut[(int)(y0 * 1024.0f)]; else r0 = powf(y0, 0.75f);
        if (y1 < 4.0f) r1 = pow075_lut[(int)(y1 * 1024.0f)]; else r1 = powf(y1, 0.75f);
        if (y2 < 4.0f) r2 = pow075_lut[(int)(y2 * 1024.0f)]; else r2 = powf(y2, 0.75f);
        if (y3 < 4.0f) r3 = pow075_lut[(int)(y3 * 1024.0f)]; else r3 = powf(y3, 0.75f);
        int q0 = (int)(r0 + magic), q1 = (int)(r1 + magic), q2 = (int)(r2 + magic), q3 = (int)(r3 + magic);
        xi[cnt]   = (v0 < 0) ? -q0 : q0;
        xi[cnt+1] = (v1 < 0) ? -q1 : q1;
        xi[cnt+2] = (v2 < 0) ? -q2 : q2;
        xi[cnt+3] = (v3 < 0) ? -q3 : q3;
    }
    for (; cnt < n; cnt++) {
        float val = xr[cnt];
        float tmp = (val < 0 ? -val : val) * sfacfix;
        float res;
        if (tmp < 4.0f) res = pow075_lut[(int)(tmp * 1024.0f)]; else res = powf(tmp, 0.75f);
        int q = (int)(res + magic);
        xi[cnt] = (val < 0) ? -q : q;
    }
}

void quantize_mxu2(const faac_real * __restrict xr, int * __restrict xi, int n, faac_real sfacfix)
{
    int cnt = 0;
    const float magic_val = (float)MAGIC_NUMBER;
    if (n >= 8) {
        __asm__ __volatile__ (
            ".set push\n\t"
            ".set noreorder\n\t"
            "lw $t4, %[sfac]\n\t"
            "li $t5, 0x7FFFFFFF\n\t"
            "lw $t6, %[magic]\n\t"
            "move $t7, $zero\n\t"
            "li $t3, 16\n\t"
            MXU2_MFCPUW(1, 12)
            MXU2_MFCPUW(2, 13)
            MXU2_MFCPUW(3, 14)
            MXU2_MFCPUW(4, 15)
            "move $t0, %[ptr_xr]\n\t"
            "move $t1, %[ptr_xi]\n\t"
            "move $t2, %[loop_cnt]\n\t"
            "1:\n\t"
            MXU2_LU1QX(10, 0, 8)
            MXU2_LU1QX(11, 11, 8)
            MXU2_FCLTW(20, 10, 4)
            MXU2_FCLTW(21, 11, 4)
            MXU2_ANDV(10, 10, 2)
            MXU2_ANDV(11, 11, 2)
            MXU2_FMULW(10, 10, 1)
            MXU2_FMULW(11, 11, 1)
            MXU2_FSQRTW(12, 10)
            MXU2_FSQRTW(13, 11)
            MXU2_FMULW(10, 10, 12)
            MXU2_FMULW(11, 11, 13)
            MXU2_FSQRTW(10, 10)
            MXU2_FSQRTW(11, 11)
            MXU2_FADDW(10, 10, 3)
            MXU2_FADDW(11, 11, 3)
            MXU2_VTRUNCSWS(12, 10)
            MXU2_VTRUNCSWS(13, 11)
            MXU2_XORV(12, 12, 20)
            MXU2_XORV(13, 13, 21)
            MXU2_SUBW(12, 12, 20)
            MXU2_SUBW(13, 13, 21)
            MXU2_SU1QX(12, 0, 9)
            MXU2_SU1QX(13, 11, 9)
            "addiu $t0, $t0, 32\n\t"
            "addiu $t1, $t1, 32\n\t"
            "addiu $t2, $t2, -1\n\t"
            "bnez $t2, 1b\n\t"
            "nop\n\t"
            "sync\n\t"
            ".set pop\n\t"
            : : [ptr_xr] "r"(xr), [ptr_xi] "r"(xi), [sfac] "m"(sfacfix), [magic] "m"(magic_val), [loop_cnt] "r"(n >> 3)
            : "$t0", "$t1", "$t2", "$t3", "$t4", "$t5", "$t6", "$t7", "memory"
        );
        cnt = (n >> 3) << 3;
    }
    for (; cnt < n; cnt++) {
        faac_real val = xr[cnt];
        faac_real tmp = (val < 0 ? -val : val) * sfacfix;
        tmp = sqrtf(tmp * sqrtf(tmp));
        int q = (int)(tmp + (faac_real)MAGIC_NUMBER);
        xi[cnt] = (val < 0) ? -q : q;
    }
}
