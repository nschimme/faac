#ifndef CPU_COMPUTE_H
#define CPU_COMPUTE_H

typedef enum {
    CPU_CAP_NONE = 0,
    CPU_CAP_SSE2 = 1 << 0,
    CPU_CAP_SSE3 = 1 << 1,
    CPU_CAP_SSSE3 = 1 << 2,
    CPU_CAP_SSE4_1 = 1 << 3,
    CPU_CAP_SSE4_2 = 1 << 4,
    CPU_CAP_AVX = 1 << 5,
    CPU_CAP_AVX2 = 1 << 6,
    CPU_CAP_NEON = 1 << 7,
} cpu_caps_t;

cpu_caps_t get_cpu_caps(void);

#endif /* CPU_COMPUTE_H */
