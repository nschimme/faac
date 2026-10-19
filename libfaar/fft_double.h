#ifndef FAAR_FFT_DOUBLE_H
#define FAAR_FFT_DOUBLE_H

#include <stddef.h>

/* Interleaved complex FFT plan. Mixed-radix execution uses per-plan scratch;
 * concurrent executions with the same plan must use external synchronization. */
typedef struct faar_fft_plan {
    size_t size;
    size_t *bit_reverse;
    double *twiddle_real;
    double *twiddle_imag;
    double *workspace;
    int radix2;
} faar_fft_plan;

typedef struct faar_real_fft_plan {
    size_t size;
    faar_fft_plan complex_plan;
    double *split_real;
    double *split_imag;
    double *scratch;
} faar_real_fft_plan;

/* Returns zero on success. Supported sizes factor entirely into 2, 3, 5, 7. */
int faar_fft_plan_init(faar_fft_plan *plan, size_t size);
void faar_fft_plan_clear(faar_fft_plan *plan);

/* Forward uses exp(-i*2*pi*k/N); inverse uses exp(+i*2*pi*k/N)/N. */
void faar_fft_execute(const faar_fft_plan *plan, double *interleaved,
                      int inverse);

/* This real interface currently requires a power-of-two size; the complex
 * interface above also supports mixed-radix sizes.
 * Forward transforms size real values into size/2+1 interleaved bins.
 * Inverse restores size real values. Reserve size+2 doubles in data. */
int faar_real_fft_plan_init(faar_real_fft_plan *plan, size_t size);
void faar_real_fft_plan_clear(faar_real_fft_plan *plan);
void faar_real_fft_execute(const faar_real_fft_plan *plan, double *data,
                           int inverse);
/* Ooura-compatible packed real spectrum: DC at 0, Nyquist at 1, then
   interleaved real/imaginary bins. Inverse execution is normalized. */
void faar_real_fft_execute_packed(const faar_real_fft_plan *plan, double *data,
                                  int inverse);

#endif
