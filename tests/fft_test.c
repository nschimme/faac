#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "libfaac/fft.h"
#include "libfaac/cpu_compute.h"

#define MAX_LOGM 9

int main() {
    FFT_Tables scalar_tables, sse2_tables;
    faac_real *xr_scalar, *xi_scalar, *xr_sse2, *xi_sse2;
    int logm, size, i;
    double max_diff = 0;

    for (logm = 1; logm <= MAX_LOGM; logm++) {
        size = 1 << logm;
        xr_scalar = malloc(size * sizeof(faac_real));
        xi_scalar = malloc(size * sizeof(faac_real));
        xr_sse2 = malloc(size * sizeof(faac_real));
        xi_sse2 = malloc(size * sizeof(faac_real));

        // Fill with random data
        for (i = 0; i < size; i++) {
            xr_scalar[i] = xr_sse2[i] = (faac_real)rand() / RAND_MAX;
            xi_scalar[i] = xi_sse2[i] = (faac_real)rand() / RAND_MAX;
        }

        fft_initialize(&scalar_tables, fft_proc_scalar);
#if defined(HAVE_SSE2)
        fft_initialize(&sse2_tables, fft_proc_sse2);
#else
        fft_initialize(&sse2_tables, fft_proc_scalar);
#endif

        fft(&scalar_tables, xr_scalar, xi_scalar, logm);
        fft(&sse2_tables, xr_sse2, xi_sse2, logm);

        for (i = 0; i < size; i++) {
            double diff_r = fabs((double)xr_scalar[i] - (double)xr_sse2[i]);
            double diff_i = fabs((double)xi_scalar[i] - (double)xi_sse2[i]);
            if (diff_r > max_diff) max_diff = diff_r;
            if (diff_i > max_diff) max_diff = diff_i;
        }

        fft_terminate(&scalar_tables);
        fft_terminate(&sse2_tables);
        free(xr_scalar);
        free(xi_scalar);
        free(xr_sse2);
        free(xi_sse2);

        printf("Logm %d: Max diff %e\n", logm, max_diff);
    }

    if (max_diff > 1e-5) {
        printf("FFT test FAILED (max_diff too large)\n");
        return 1;
    }

    printf("FFT test PASSED\n");
    return 0;
}
