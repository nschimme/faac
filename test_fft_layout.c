#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "libfaac/fft.h"

int main() {
    int logm = 10;
    int size = 1 << logm;
    FFT_Tables tables;
    fft_initialize(&tables);
    faac_real *x = calloc(size, sizeof(faac_real));
    // Sine at bin 10, shifted by 90 deg (sine)
    for (int i=0; i<size; i++) x[i] = sin(2.0 * M_PI * 10 * i / size);
    rfft(&tables, x, logm);
    for (int i=0; i<size; i++) {
        if (fabs(x[i]) > 1.0) printf("x[%d] = %f\n", i, x[i]);
    }
    fft_terminate(&tables);
    free(x);
    return 0;
}
