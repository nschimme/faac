/* FAAR real FFT research interface. LGPL-2.1-or-later. */
#ifndef FAAR_FFT_REAL_H
#define FAAR_FFT_REAL_H

/* Plans own mutable scratch: executions on one plan must be serialized.
 * Sizes are powers of two, 256 through 65536. Init returns 0 on success.
 * Packed spectrum: DC, Nyquist, then (real, positive-sine imaginary) bins,
 * matching faar_real_fft_execute_packed. Data contains exactly N scalars.
 * Init requires a fresh or cleared plan. Inverse is normalized. Clear accepts a zero-initialized plan. */
typedef struct faar_rfftf_plan {
    int size, logm;
    float *twiddle, *split, *work;
} faar_rfftf_plan;

int faar_rfftf_init(faar_rfftf_plan *plan, int size);
void faar_rfftf_clear(faar_rfftf_plan *plan);
void faar_rfftf_execute(const faar_rfftf_plan *plan, float *data, int inverse);

/* Complex transform of n points, 16 through 4194304, on the same plan type.
 * Data is planar: n real parts then n imaginary parts, e^{-j} convention and
 * unnormalized. The plan owns two such buffers (cbuffer 0 and 1). cforward
 * transforms the buffer `in`, which must be one of them, and returns the
 * buffer holding the result: either one, so the caller never aliases it.
 * The inverse is conj(cforward(conj(x))) / n. */
int faar_rfftf_cinit(faar_rfftf_plan *plan, int n);
float *faar_rfftf_cbuffer(const faar_rfftf_plan *plan, int which);
float *faar_rfftf_cforward(const faar_rfftf_plan *plan, float *in);

typedef struct faar_rfftd_plan {
    int size, logm;
    double *twiddle, *split, *work;
} faar_rfftd_plan;

int faar_rfftd_init(faar_rfftd_plan *plan, int size);
void faar_rfftd_clear(faar_rfftd_plan *plan);
void faar_rfftd_execute(const faar_rfftd_plan *plan, double *data, int inverse);
int faar_rfftd_cinit(faar_rfftd_plan *plan, int n);
double *faar_rfftd_cbuffer(const faar_rfftd_plan *plan, int which);
double *faar_rfftd_cforward(const faar_rfftd_plan *plan, double *in);

#endif
