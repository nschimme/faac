#include "cpu_compute.h"

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#ifdef _MSC_VER
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#endif

#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

cpu_caps_t get_cpu_caps(void)
{
    static cpu_caps_t caps = CPU_CAP_NONE;
    static int initialized = 0;

    if (initialized) return caps;

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    int cpu_info[4];
#ifdef _MSC_VER
    __cpuid(cpu_info, 1);
#else
    __cpuid(1, cpu_info[0], cpu_info[1], cpu_info[2], cpu_info[3]);
#endif

    if (cpu_info[3] & (1 << 26)) caps |= CPU_CAP_SSE2;
    if (cpu_info[2] & (1 << 0))  caps |= CPU_CAP_SSE3;
    if (cpu_info[2] & (1 << 9))  caps |= CPU_CAP_SSSE3;
    if (cpu_info[2] & (1 << 19)) caps |= CPU_CAP_SSE4_1;
    if (cpu_info[2] & (1 << 20)) caps |= CPU_CAP_SSE4_2;
    if (cpu_info[2] & (1 << 28)) caps |= CPU_CAP_AVX;

    if (caps & CPU_CAP_AVX) {
#ifdef _MSC_VER
        __cpuidex(cpu_info, 7, 0);
#else
        __cpuid_count(7, 0, cpu_info[0], cpu_info[1], cpu_info[2], cpu_info[3]);
#endif
        if (cpu_info[1] & (1 << 5)) caps |= CPU_CAP_AVX2;
    }
#endif

#if defined(__aarch64__) || defined(__arm__)
#ifdef __APPLE__
    int val = 0;
    size_t len = sizeof(val);
    if (sysctlbyname("hw.optional.neon", &val, &len, NULL, 0) == 0 && val) {
        caps |= CPU_CAP_NEON;
    }
#else
    // For Linux ARM, one could read /proc/cpuinfo or use getauxval(AT_HWCAP)
    // For now, assume NEON on aarch64
#ifdef __aarch64__
    caps |= CPU_CAP_NEON;
#endif
#endif
#endif

    initialized = 1;
    return caps;
}
