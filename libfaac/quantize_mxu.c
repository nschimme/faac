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

/*
 * 4096-entry LUT for x^0.75 calculation.
 */
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
static void mxu_crash_handler(int sig)
{
    siglongjmp(mxu_jmpbuf, 1);
}

static inline void enable_mxu_kernel(void)
{
    int ret1 = prctl(PR_SET_MXU, 1, 0, 0, 0);
    int ret2 = prctl(31, 1, 0, 0, 0);
    if (ret1 < 0 && ret2 < 0) {
        fprintf(stderr, "MXU: Failed to enable via prctl (30/31): %s\n", strerror(errno));
    } else {
        fprintf(stderr, "MXU: Kernel enablement (prctl 30/31) reported success.\n");
    }
}

int check_mxu1_support(void)
{
    struct sigaction sa, old_sa_ill, old_sa_bus, old_sa_segv;
    int supported = 0;
    int xburst = 0;

    FILE *f = fopen("/proc/cpuinfo", "r");
    if (f) {
        char line[1024];
        while (fgets(line, sizeof(line), f)) {
            if (strstr(line, "Xburst") || strstr(line, "Ingenic")) {
                xburst = 1;
                break;
            }
        }
        fclose(f);
    }

    if (!xburst) return 0;

    sa.sa_handler = mxu_crash_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGILL, &sa, &old_sa_ill);
    sigaction(SIGBUS, &sa, &old_sa_bus);
    sigaction(SIGSEGV, &sa, &old_sa_segv);

    if (sigsetjmp(mxu_jmpbuf, 1) == 0) {
        enable_mxu_kernel();

        int enable_val = 3;
        /* Force engine activation via S32I2M XR16, $t0
           Encoding: Major(28) rs(0) rt(8) rd(0) sa(16) funct(47) = 0x7008042f
        */
        __asm__ __volatile__ (
            "move $t0, %0\n\t"
            ".word 0x7008042f\n\t"
            "nop; nop; nop\n\t"
            "sync\n\t"
            : : "r"(enable_val) : "$t0"
        );

        /* Try reading XR0 (always 0) using S32M2I $t0, XR0
           Encoding: Major(28) rs(0) rt(8) rd(0) sa(0) funct(46) = 0x7008002e
        */
        int val = 0xdead;
        __asm__ __volatile__ (
            "li $t0, 0xbeef\n\t"
            ".word 0x7008002e\n\t"
            "move %0, $t0\n\t"
            : "=r"(val) : : "$t0"
        );
        if (val == 0) supported = 1;
    }

    sigaction(SIGILL, &old_sa_ill, NULL);
    sigaction(SIGBUS, &old_sa_bus, NULL);
    sigaction(SIGSEGV, &old_sa_segv, NULL);

    fprintf(stderr, "MXU: Instruction probe for MXU1 (XBurst core): %s\n", supported ? "SUCCESS" : "FAILED");
    return supported;
}

int check_mxu2_support(void)
{
    struct sigaction sa, old_sa_ill, old_sa_bus, old_sa_segv;
    int supported = 0;
    int xburst = 0;

    FILE *f = fopen("/proc/cpuinfo", "r");
    if (f) {
        char line[1024];
        while (fgets(line, sizeof(line), f)) {
            if (strstr(line, "Xburst") || strstr(line, "Ingenic")) {
                xburst = 1;
                break;
            }
        }
        fclose(f);
    }

    if (!xburst) return 0;

    sa.sa_handler = mxu_crash_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGILL, &sa, &old_sa_ill);
    sigaction(SIGBUS, &sa, &old_sa_bus);
    sigaction(SIGSEGV, &sa, &old_sa_segv);

    if (sigsetjmp(mxu_jmpbuf, 1) == 0) {
        enable_mxu_kernel();

        int enable_val = 3;
        /* Force engine activation via S32I2M XR16, $t0 */
        __asm__ __volatile__ (
            "move $t0, %0\n\t"
            ".word 0x7008042f\n\t"
            "nop; nop; nop\n\t"
            "sync\n\t"
            : : "r"(enable_val) : "$t0"
        );

        int mir = 0;
        __asm__ __volatile__ (
            "li $t0, 0\n\t"
            /* Try reading MIR using CFCMXU $t0, MIR (reg 0)
               Encoding from docs: Major(18) rs(30) rt(8) rd(0) sa(30) funct(61) = 0x4bc8f03d
            */
            ".word 0x4bc8f03d\n\t"
            "move %0, $t0\n\t"
            : "=r"(mir) : : "$t0"
        );
        if (mir != 0) supported = 1;
    }

    sigaction(SIGILL, &old_sa_ill, NULL);
    sigaction(SIGBUS, &old_sa_bus, NULL);
    sigaction(SIGSEGV, &old_sa_segv, NULL);

    fprintf(stderr, "MXU: Instruction probe for MXU2 (XBurst core): %s\n", supported ? "SUCCESS" : "FAILED");
    return supported;
}

void get_cpu_info(char *buf, size_t len)
{
    FILE *f = fopen("/proc/cpuinfo", "r");
    buf[0] = '\0';
    if (!f) {
        strncpy(buf, "Error: cannot open /proc/cpuinfo", len);
        return;
    }
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "cpu model") || strstr(line, "ASEs implemented") || strstr(line, "BogoMIPS")) {
            strncat(buf, line, len - strlen(buf) - 1);
        }
    }
    fclose(f);
}

unsigned int get_mips_prid(void)
{
    unsigned int prid = 0;
    struct sigaction sa, old_sa_ill;
    sa.sa_handler = mxu_crash_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGILL, &sa, &old_sa_ill) == 0) {
        if (sigsetjmp(mxu_jmpbuf, 1) == 0) {
            __asm__ __volatile__ ("mfc0 %0, $15, 0" : "=r"(prid));
        }
        sigaction(SIGILL, &old_sa_ill, NULL);
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
        float v0 = xr[cnt];
        float v1 = xr[cnt+1];
        float v2 = xr[cnt+2];
        float v3 = xr[cnt+3];

        float y0, y1, y2, y3;

        y0 = (v0 < 0 ? -v0 : v0) * sfacfix;
        y1 = (v1 < 0 ? -v1 : v1) * sfacfix;
        y2 = (v2 < 0 ? -v2 : v2) * sfacfix;
        y3 = (v3 < 0 ? -v3 : v3) * sfacfix;

        float r0, r1, r2, r3;

        if (y0 < 4.0f) r0 = pow075_lut[(int)(y0 * 1024.0f)]; else r0 = powf(y0, 0.75f);
        if (y1 < 4.0f) r1 = pow075_lut[(int)(y1 * 1024.0f)]; else r1 = powf(y1, 0.75f);
        if (y2 < 4.0f) r2 = pow075_lut[(int)(y2 * 1024.0f)]; else r2 = powf(y2, 0.75f);
        if (y3 < 4.0f) r3 = pow075_lut[(int)(y3 * 1024.0f)]; else r3 = powf(y3, 0.75f);

        int q0 = (int)(r0 + magic);
        int q1 = (int)(r1 + magic);
        int q2 = (int)(r2 + magic);
        int q3 = (int)(r3 + magic);

        xi[cnt]   = (v0 < 0) ? -q0 : q0;
        xi[cnt+1] = (v1 < 0) ? -q1 : q1;
        xi[cnt+2] = (v2 < 0) ? -q2 : q2;
        xi[cnt+3] = (v3 < 0) ? -q3 : q3;
    }

    for (; cnt < n; cnt++)
    {
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

            MXU2_MFCPUW(1, 12)      // $t4=12
            MXU2_MFCPUW(2, 13)      // $t5=13
            MXU2_MFCPUW(3, 14)      // $t6=14
            MXU2_MFCPUW(4, 15)      // $t7=15

            "move $t0, %[ptr_xr]\n\t"
            "move $t1, %[ptr_xi]\n\t"
            "move $t2, %[loop_cnt]\n\t"

            "1:\n\t"
            MXU2_LU1QX(10, 0, 8)    // Load xr[0..3]
            MXU2_LU1QX(11, 11, 8)   // Load xr[4..7] ($t3=11)

            MXU2_FCLTW(20, 10, 4)   // Sign mask A
            MXU2_FCLTW(21, 11, 4)   // Sign mask B

            MXU2_ANDV(10, 10, 2)    // Abs xr[0..3]
            MXU2_ANDV(11, 11, 2)    // Abs xr[4..7]

            MXU2_FMULW(10, 10, 1)   // sfac A
            MXU2_FMULW(11, 11, 1)   // sfac B

            MXU2_FSQRTW(12, 10)     // Interleave pipeline
            MXU2_FSQRTW(13, 11)

            MXU2_FMULW(10, 10, 12)
            MXU2_FMULW(11, 11, 13)

            MXU2_FSQRTW(10, 10)
            MXU2_FSQRTW(11, 11)

            MXU2_FADDW(10, 10, 3)
            MXU2_FADDW(11, 11, 3)

            MXU2_VTRUNCSWS(12, 10)
            MXU2_VTRUNCSWS(13, 11)

            MXU2_XORV(12, 12, 20)   // apply sign A
            MXU2_XORV(13, 13, 21)   // apply sign B
            MXU2_SUBW(12, 12, 20)
            MXU2_SUBW(13, 13, 21)

            MXU2_SU1QX(12, 0, 9)    // Store xi[0..3]
            MXU2_SU1QX(13, 11, 9)   // Store xi[4..7] ($t3=11)

            "addiu $t0, $t0, 32\n\t"
            "addiu $t1, $t1, 32\n\t"
            "addiu $t2, $t2, -1\n\t"
            "bnez $t2, 1b\n\t"
            "nop\n\t"
            "sync\n\t"

            ".set pop\n\t"
            :
            : [ptr_xr] "r"(xr), [ptr_xi] "r"(xi),
              [sfac] "m"(sfacfix), [magic] "m"(magic_val), [loop_cnt] "r"(n >> 3)
            : "$t0", "$t1", "$t2", "$t3", "$t4", "$t5", "$t6", "$t7", "memory"
        );
        cnt = (n >> 3) << 3;
    }

    /* Scalar remainder */
    for (; cnt < n; cnt++)
    {
        faac_real val = xr[cnt];
        faac_real tmp = (val < 0 ? -val : val) * sfacfix;
        tmp = sqrtf(tmp * sqrtf(tmp));
        int q = (int)(tmp + (faac_real)MAGIC_NUMBER);
        xi[cnt] = (val < 0) ? -q : q;
    }
}
