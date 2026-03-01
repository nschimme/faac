#ifndef CPU_COMPUTE_H
#define CPU_COMPUTE_H

typedef enum {
    CPU_CAP_NONE = 0,
    CPU_CAP_SSE2 = (1 << 0),
    CPU_CAP_AVX2 = (1 << 1),
    CPU_CAP_NEON = (1 << 2)
} CPUCaps;

unsigned int get_cpu_caps(void);

#endif
