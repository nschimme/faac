/* The mixed-radix transforms behind the 960- and 120-point DCT-IVs, against a
 * direct DFT: a wrong butterfly shows up as an error of order one. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "fft.h"

static int check(const char *name, int n, void (*f)(float *, float *), int logm)
{
    float *x = malloc(sizeof(float) * 4 * n), *y = x + 2 * n, *ref = malloc(sizeof(float) * 2 * n);
    double err = 0.0, norm = 0.0;
    for (int i = 0; i < 2 * n; i++) ref[i] = x[i] = (float)(rand() % 2001 - 1000) / 1000.0f;
    if (f) f(x, y);
    else fft(x, y, logm);
    for (int k = 0; k < n; k++) {
        double re = 0.0, im = 0.0;
        for (int j = 0; j < n; j++) {
            double a = -2.0 * M_PI * (double)((long)j * k % n) / n;
            re += ref[j] * cos(a) - ref[n + j] * sin(a);
            im += ref[j] * sin(a) + ref[n + j] * cos(a);
        }
        err += (y[k] - re) * (y[k] - re) + (y[n + k] - im) * (y[n + k] - im);
        norm += re * re + im * im;
    }
    free(x);
    free(ref);
    double rel = sqrt(err / norm);
    if (rel > 1e-5) {
        fprintf(stderr, "%s (%d points): relative error %g\n", name, n, rel);
        return 1;
    }
    return 0;
}

int main(void)
{
    int failed = 0;
    fft_init();
    failed |= check("fft", 64, NULL, 6);
    failed |= check("fft", 512, NULL, 9);
#ifndef FAAD_DISABLE_DAB
    failed |= check("fft", 32, NULL, 5);
    failed |= check("fft60", 60, fft60, 0);
    failed |= check("fft480", 480, fft480, 0);
#endif
    return failed;
}
