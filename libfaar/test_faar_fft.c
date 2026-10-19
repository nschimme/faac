#include "fft_double.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846264338327950288
#endif

static int fail(const char *message) {
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

static int test_round_trip(void) {
    enum { N = 1024 };
    faar_fft_plan plan;
    double data[N * 2], original[N * 2];
    uint32_t state = 0x51f15e5du;
    if (faar_fft_plan_init(&plan, N)) return fail("FFT plan init failed");
    for (size_t i = 0; i < N * 2; ++i) {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        data[i] = (double)(int32_t)state / 2147483648.0;
        original[i] = data[i];
    }
    faar_fft_execute(&plan, data, 0);
    faar_fft_execute(&plan, data, 1);
    for (size_t i = 0; i < N * 2; ++i) {
        if (fabs(data[i] - original[i]) > 2e-13) {
            faar_fft_plan_clear(&plan);
            return fail("forward/inverse round trip exceeded 2e-13");
        }
    }
    faar_fft_plan_clear(&plan);
    return 0;
}

static int test_against_direct_dft(void) {
    enum { N = 32 };
    faar_fft_plan plan;
    double data[N * 2];
    if (faar_fft_plan_init(&plan, N)) return fail("small FFT plan init failed");
    for (size_t n = 0; n < N; ++n) {
        data[2 * n] = sin(0.17 * n) + 0.2 * cos(0.43 * n);
        data[2 * n + 1] = 0.0;
    }
    faar_fft_execute(&plan, data, 0);
    for (size_t k = 0; k < N; ++k) {
        double real = 0.0, imag = 0.0;
        for (size_t n = 0; n < N; ++n) {
            double angle = -2.0 * M_PI * (double)(k * n) / N;
            real += (sin(0.17 * n) + 0.2 * cos(0.43 * n)) * cos(angle);
            imag += (sin(0.17 * n) + 0.2 * cos(0.43 * n)) * sin(angle);
        }
        if (fabs(data[2 * k] - real) > 2e-12
                || fabs(data[2 * k + 1] - imag) > 2e-12) {
            faar_fft_plan_clear(&plan);
            return fail("FFT did not match direct DFT");
        }
    }
    faar_fft_plan_clear(&plan);
    return 0;
}

static int test_mixed_sizes(void) {
    static const size_t sizes[] = {12, 45, 70, 105};
    for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s) {
        const size_t n = sizes[s];
        faar_fft_plan plan;
        double *data = (double *)malloc(2 * n * sizeof(double));
        double *original = (double *)malloc(2 * n * sizeof(double));
        if (!data || !original || faar_fft_plan_init(&plan, n)) {
            free(data); free(original);
            return fail("mixed-radix plan allocation failed");
        }
        for (size_t i = 0; i < n; ++i) {
            data[2*i] = original[2*i] = sin(0.13 * i) + cos(0.31 * i) * 0.2;
            data[2*i+1] = original[2*i+1] = cos(0.19 * i) * 0.1;
        }
        faar_fft_execute(&plan, data, 0);
        for (size_t k = 0; k < n; ++k) {
            double re = 0.0, im = 0.0;
            for (size_t j = 0; j < n; ++j) {
                const double a = -2.0 * M_PI * (double)(j * k) / (double)n;
                re += original[2*j] * cos(a) - original[2*j+1] * sin(a);
                im += original[2*j] * sin(a) + original[2*j+1] * cos(a);
            }
            if (fabs(data[2*k] - re) > 3e-12 || fabs(data[2*k+1] - im) > 3e-12) {
                faar_fft_plan_clear(&plan); free(data); free(original);
                return fail("mixed-radix FFT did not match direct DFT");
            }
        }
        faar_fft_execute(&plan, data, 1);
        for (size_t i = 0; i < 2*n; ++i) {
            if (fabs(data[i] - original[i]) > 3e-13) {
                faar_fft_plan_clear(&plan); free(data); free(original);
                return fail("mixed-radix round trip failed");
            }
        }
        faar_fft_plan_clear(&plan); free(data); free(original);
    }
    return 0;
}

static int test_real_fft(void) {
    enum { N = 64 };
    faar_real_fft_plan plan;
    double data[N + 2], original[N];
    if (faar_real_fft_plan_init(&plan, N))
        return fail("real FFT plan init failed");
    for (size_t n = 0; n < N; ++n)
        data[n] = original[n] = sin(0.11 * n) + 0.3 * cos(0.37 * n);
    faar_real_fft_execute(&plan, data, 0);
    for (size_t k = 0; k <= N / 2; ++k) {
        double real = 0.0, imag = 0.0;
        for (size_t n = 0; n < N; ++n) {
            double angle = -2.0 * M_PI * (double)(k * n) / N;
            real += original[n] * cos(angle);
            imag += original[n] * sin(angle);
        }
        if (fabs(data[2 * k] - real) > 2e-12
                || fabs(data[2 * k + 1] - imag) > 2e-12) {
            fprintf(stderr, "real FFT bin %zu: got %.17g %.17g expected %.17g %.17g\n",
                    k, data[2 * k], data[2 * k + 1], real, imag);
            faar_real_fft_plan_clear(&plan);
            return fail("real FFT did not match direct DFT");
        }
    }
    faar_real_fft_execute(&plan, data, 1);
    for (size_t n = 0; n < N; ++n) {
        if (fabs(data[n] - original[n]) > 2e-13) {
            faar_real_fft_plan_clear(&plan);
            return fail("real FFT inverse round trip failed");
        }
    }
    faar_real_fft_plan_clear(&plan);
    return 0;
}

int main(void) {
    faar_fft_plan invalid;
    if (!faar_fft_plan_init(&invalid, 11))
        return fail("unsupported FFT length was accepted");
    if (test_round_trip() || test_against_direct_dft() || test_mixed_sizes()
            || test_real_fft()) return 1;
    puts("double FFT: complex radix-2, mixed-radix and real transforms passed");
    return 0;
}
