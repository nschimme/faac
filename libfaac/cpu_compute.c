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
#include "quantize.h"
#include "fft.h"

#if defined(SSE2_ARCH)
# ifdef _MSC_VER
#  include <intrin.h>
# elif defined(__GNUC__) || defined(__clang__)
#  include <cpuid.h>
# endif
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

        if (ecx & (1 << 28)) { // AVX
            /* Check if OS saves YMM state */
            unsigned long long xcr0 = 0;
# ifdef _MSC_VER
            xcr0 = _xgetbv(0);
# elif defined(__GNUC__) || defined(__clang__)
            unsigned int low, high;
            __asm__ volatile("xgetbv" : "=a"(low), "=d"(high) : "c"(0));
            xcr0 = ((unsigned long long)high << 32) | low;
# endif
            if ((xcr0 & 6) == 6) { // XMM and YMM state saved by OS
                if (max_leaf >= 7) {
                    unsigned int eax7 = 0, ebx7 = 0, ecx7 = 0, edx7 = 0;
# ifdef _MSC_VER
                    __cpuid(cpu_info, 7);
                    ebx7 = (unsigned int)cpu_info[1];
# elif defined(__GNUC__) || defined(__clang__)
                    __cpuid_count(7, 0, eax7, ebx7, ecx7, edx7);
# endif
                    if (ebx7 & (1 << 5)) // AVX2
                        caps |= CPU_CAP_AVX2;
                }
                if (ecx & (1 << 12)) // FMA
                    caps |= CPU_CAP_FMA;
            }
        }
    }
#endif

    return caps;
}

void init_simd_functions(SimdFunctions *simd, CPUCaps caps)
{
    simd->quantize = quantize_scalar;
    simd->fft_proc = fft_proc_scalar;

#if defined(HAVE_SSE2)
    if (caps & CPU_CAP_SSE2) {
        simd->quantize = quantize_sse2;
        simd->fft_proc = fft_proc_sse2;
    }
#endif

#if defined(HAVE_AVX2)
    if (caps & CPU_CAP_AVX2) {
        simd->fft_proc = fft_proc_avx2;
    }
#endif
}
