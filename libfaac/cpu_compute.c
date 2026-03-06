/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2026 Nils Schimmelmann
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "cpu_compute.h"
#include <signal.h>
#include <setjmp.h>
#include "mxu2_asm.h"

#if defined(SSE2_ARCH)
# ifdef _MSC_VER
#  include <intrin.h>
# elif defined(__GNUC__) || defined(__clang__)
#  include <cpuid.h>
# endif
#endif

#if defined(__mips__)
static sigjmp_buf jmpbuf;
static void sigill_handler(int sig)
{
    siglongjmp(jmpbuf, 1);
}
#endif

CPUCaps get_cpu_caps(void)
{
    CPUCaps caps = CPU_CAP_NONE;

#if defined(SSE2_ARCH)
    unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
    unsigned int max_leaf = 0;

# ifdef _MSC_VER
    int cpu_info[4] = {0};
    __cpuid(cpu_info, 0);
    max_leaf = (unsigned int)cpu_info[0];
# elif defined(__GNUC__) || defined(__clang__)
    __cpuid(0, max_leaf, ebx, ecx, edx);
# endif

    if (max_leaf >= 1) {
# ifdef _MSC_VER
        __cpuid(cpu_info, 1);
        eax = (unsigned int)cpu_info[0];
        ebx = (unsigned int)cpu_info[1];
        ecx = (unsigned int)cpu_info[2];
        edx = (unsigned int)cpu_info[3];
# elif defined(__GNUC__) || defined(__clang__)
        __get_cpuid(1, &eax, &ebx, &ecx, &edx);
# endif
        if (edx & (1 << 26)) // SSE2
            caps |= CPU_CAP_SSE2;
    }
#endif

#if defined(__mips__)
    struct sigaction sa, old_sa;
    sa.sa_handler = sigill_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGILL, &sa, &old_sa) == 0) {
        if (sigsetjmp(jmpbuf, 1) == 0) {
            // Enable MXU first
            int val = 3;
            __asm__ __volatile__ (
                "move $t0, %0\n\t"
                MXU_S32I2M(16, 8) // XR16 = $t0 (8 is $t0)
                "nop\n\t"
                "nop\n\t"
                "nop\n\t"
                : : "r"(val) : "$t0"
            );

            // Try to read MXU2 MIR
            int mir = 0;
            __asm__ __volatile__ (
                MXU2_CFCMXU(8, 0) // $t0 = vr0 (MIR is 0)
                "move %0, $t0\n\t"
                : "=r"(mir) : : "$t0"
            );

            // If we didn't crash, it's MXU2
            caps |= CPU_CAP_MXU2;
        }
        sigaction(SIGILL, &old_sa, NULL);
    }
#endif

    return caps;
}
