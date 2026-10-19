/*
 * Small double-precision radix-2 FFT for FAAR's CPU DFT rate stage.
 * Independently implemented; no SoX or Tuneout implementation code is used.
 */

#include "fft_double.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>


#ifndef M_PI
#define M_PI 3.14159265358979323846264338327950288
#endif

static int is_power_of_two(size_t n) {
    return n && !(n & (n - 1));
}

static int is_smooth2357(size_t n) {
    if (!n) return 0;
    for (size_t p = 2; p <= 7; p = p == 2 ? 3 : p + 2) {
        if (p == 3 || p == 5 || p == 7 || p == 2)
            while (n % p == 0) n /= p;
    }
    return n == 1;
}

static size_t smallest_factor(size_t n) {
    if (!(n % 2)) return 2;
    if (!(n % 3)) return 3;
    if (!(n % 5)) return 5;
    if (!(n % 7)) return 7;
    return n;
}

static void mixed_fft(const faar_fft_plan *plan, const double *input,
                      size_t stride, double *output, double *temporary,
                      size_t n, size_t root_stride, int inverse) {
    if (n == 1) {
        output[0] = input[0];
        output[1] = input[1];
        return;
    }

    const size_t radix = smallest_factor(n);
    const size_t sub_size = n / radix;
    for (size_t j = 0; j < radix; ++j) {
        mixed_fft(plan, input + 2 * j * stride, stride * radix,
                  output + 2 * j * sub_size, temporary, sub_size,
                  root_stride * radix, inverse);
    }

    for (size_t k = 0; k < sub_size; ++k) {
        for (size_t l = 0; l < radix; ++l) {
            const size_t frequency = k + l * sub_size;
            double sum_real = 0.0, sum_imag = 0.0;
            for (size_t j = 0; j < radix; ++j) {
                const size_t input_index = j * sub_size + k;
                const size_t table_index = (j * frequency * root_stride) % plan->size;
                double wr = plan->twiddle_real[table_index];
                double wi = plan->twiddle_imag[table_index];
                if (!inverse) wi = -wi;
                const double xr = output[2 * input_index];
                const double xi = output[2 * input_index + 1];
                sum_real += wr * xr - wi * xi;
                sum_imag += wr * xi + wi * xr;
            }
            const size_t out_index = l * sub_size + k;
            temporary[2 * out_index] = sum_real;
            temporary[2 * out_index + 1] = sum_imag;
        }
    }
    memcpy(output, temporary, 2 * n * sizeof(double));
}

int faar_fft_plan_init(faar_fft_plan *plan, size_t size) {
    if (!plan || !is_smooth2357(size)
            || size > SIZE_MAX / sizeof(size_t)
            || size > SIZE_MAX / (8 * sizeof(double)))
        return -1;

    memset(plan, 0, sizeof(*plan));
    plan->size = size;
    plan->radix2 = is_power_of_two(size);
    plan->bit_reverse = plan->radix2
        ? (size_t *)malloc(size * sizeof(*plan->bit_reverse)) : NULL;
    const size_t twiddle_count = plan->radix2 ? (size / 2 ? size / 2 : 1) : size;
    plan->twiddle_real = (double *)malloc(twiddle_count
                                         * sizeof(*plan->twiddle_real));
    plan->twiddle_imag = (double *)malloc(twiddle_count
                                         * sizeof(*plan->twiddle_imag));
    plan->workspace = plan->radix2 ? NULL : (double *)malloc(4 * size * sizeof(double));
    if ((plan->radix2 && !plan->bit_reverse) || !plan->twiddle_real
            || !plan->twiddle_imag || (!plan->radix2 && !plan->workspace)) {
        faar_fft_plan_clear(plan);
        return -1;
    }

    if (plan->radix2) {
        size_t bits = 0;
        for (size_t n = size; n > 1; n >>= 1) ++bits;
        for (size_t i = 0; i < size; ++i) {
            size_t x = i, reversed = 0;
            for (size_t b = 0; b < bits; ++b) {
                reversed = (reversed << 1) | (x & 1u);
                x >>= 1;
            }
            plan->bit_reverse[i] = reversed;
        }
    }

    for (size_t k = 0; k < twiddle_count; ++k) {
        double angle = 2.0 * M_PI * (double)k / (double)size;
        plan->twiddle_real[k] = cos(angle);
        plan->twiddle_imag[k] = sin(angle);
    }
    return 0;
}

void faar_fft_plan_clear(faar_fft_plan *plan) {
    if (!plan) return;
    free(plan->bit_reverse);
    free(plan->twiddle_real);
    free(plan->twiddle_imag);
    free(plan->workspace);
    memset(plan, 0, sizeof(*plan));
}

void faar_fft_execute(const faar_fft_plan *plan, double *data, int inverse) {
    if (!plan || !data || !plan->size) return;
    const size_t size = plan->size;

    if (!plan->radix2) {
        double *temporary = plan->workspace + 2 * size;
        mixed_fft(plan, data, 1, plan->workspace, temporary, size, 1, inverse);
        memcpy(data, plan->workspace, 2 * size * sizeof(double));
        if (inverse) {
            double scale = 1.0 / (double)size;
            for (size_t i = 0; i < 2 * size; ++i) data[i] *= scale;
        }
        return;
    }

    for (size_t i = 0; i < size; ++i) {
        size_t j = plan->bit_reverse[i];
        if (j > i) {
            double real = data[2 * i], imag = data[2 * i + 1];
            data[2 * i] = data[2 * j];
            data[2 * i + 1] = data[2 * j + 1];
            data[2 * j] = real;
            data[2 * j + 1] = imag;
        }
    }

    for (size_t length = 2; length <= size; length <<= 1) {
        size_t half = length >> 1;
        size_t twiddle_step = size / length;
        for (size_t base = 0; base < size; base += length) {
            for (size_t j = 0; j < half; ++j) {
                size_t even = base + j;
                size_t odd = even + half;
                size_t twiddle = j * twiddle_step;
                double wr = plan->twiddle_real[twiddle];
                double wi = plan->twiddle_imag[twiddle];
                if (!inverse) wi = -wi;

                double or = data[2 * odd], oi = data[2 * odd + 1];
                double tr = wr * or - wi * oi;
                double ti = wr * oi + wi * or;
                double er = data[2 * even], ei = data[2 * even + 1];
                data[2 * even] = er + tr;
                data[2 * even + 1] = ei + ti;
                data[2 * odd] = er - tr;
                data[2 * odd + 1] = ei - ti;
            }
        }
        if (length == size) break; /* Avoid size_t overflow on the final shift. */
    }

    if (inverse) {
        double scale = 1.0 / (double)size;
        for (size_t i = 0; i < 2 * size; ++i) data[i] *= scale;
    }
}

int faar_real_fft_plan_init(faar_real_fft_plan *plan, size_t size) {
    if (!plan || size < 2 || (size & (size - 1))
            || size > SIZE_MAX / (sizeof(double) * 2)) return -1;
    memset(plan, 0, sizeof(*plan));
    plan->size = size;
    size_t half = size / 2;
    if (faar_fft_plan_init(&plan->complex_plan, half)) return -1;
    plan->split_real = (double *)malloc((half + 1) * sizeof(double));
    plan->split_imag = (double *)malloc((half + 1) * sizeof(double));
    plan->scratch = (double *)malloc(size * sizeof(double));
    if (!plan->split_real || !plan->split_imag || !plan->scratch) {
        faar_real_fft_plan_clear(plan);
        return -1;
    }
    for (size_t k = 0; k <= half; ++k) {
        double angle = 2.0 * M_PI * (double)k / (double)size;
        plan->split_real[k] = cos(angle);
        plan->split_imag[k] = -sin(angle);
    }
    return 0;
}

void faar_real_fft_plan_clear(faar_real_fft_plan *plan) {
    if (!plan) return;
    faar_fft_plan_clear(&plan->complex_plan);
    free(plan->split_real);
    free(plan->split_imag);
    free(plan->scratch);
    memset(plan, 0, sizeof(*plan));
}

void faar_real_fft_execute(const faar_real_fft_plan *plan, double *data,
                           int inverse) {
    if (!plan || !data || !plan->size || !plan->scratch) return;
    const size_t half = plan->size / 2;
    double *scratch = plan->scratch;
    if (!inverse) {
        for (size_t i = 0; i < half; ++i) {
            scratch[2 * i] = data[2 * i];
            scratch[2 * i + 1] = data[2 * i + 1];
        }
        faar_fft_execute(&plan->complex_plan, scratch, 0);
        for (size_t k = 0; k <= half; ++k) {
            size_t a_index = k % half;
            size_t b_index = (half - k) % half;
            double ar = scratch[2 * a_index];
            double ai = scratch[2 * a_index + 1];
            double br = scratch[2 * b_index];
            double bi = -scratch[2 * b_index + 1];
            double dr = ar - br, di = ai - bi;
            double wr = plan->split_real[k], wi = plan->split_imag[k];
            double pr = wr * dr - wi * di;
            double pi = wr * di + wi * dr;
            data[2 * k] = 0.5 * (ar + br + pi);
            data[2 * k + 1] = 0.5 * (ai + bi - pr);
        }
    } else {
        for (size_t k = 0; k < half; ++k) {
            size_t mirror = half - k;
            double pr = data[2 * k], pi = data[2 * k + 1];
            double qr = data[2 * mirror], qi = -data[2 * mirror + 1];
            double dr = pr - qr, di = pi - qi;
            double wr = plan->split_real[k], wi = -plan->split_imag[k];
            double tr = wr * dr - wi * di;
            double ti = wr * di + wi * dr;
            scratch[2 * k] = 0.5 * (pr + qr - ti);
            scratch[2 * k + 1] = 0.5 * (pi + qi + tr);
        }
        faar_fft_execute(&plan->complex_plan, scratch, 1);
        for (size_t i = 0; i < half; ++i) {
            data[2 * i] = scratch[2 * i];
            data[2 * i + 1] = scratch[2 * i + 1];
        }
    }
}

void faar_real_fft_execute_packed(const faar_real_fft_plan *plan, double *data,
                                 int inverse) {
    if (!plan || !data || !plan->size) return;
    if (!plan->scratch) return;
    const size_t n = plan->size;
    if (!inverse) {
        faar_real_fft_execute(plan, data, 0);
        const double nyquist = data[n];
        for (size_t k = 1; k < n / 2; ++k)
            data[2 * k + 1] = -data[2 * k + 1];
        data[1] = nyquist;
    } else {
        data[n] = data[1];
        data[1] = 0.0;
        for (size_t k = 1; k < n / 2; ++k)
            data[2 * k + 1] = -data[2 * k + 1];
        faar_real_fft_execute(plan, data, 1);
    }
}
