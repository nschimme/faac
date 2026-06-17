/* QMF analysis harness: compares production SBR QMF kernels
 * against the standalone oracle (tests/qmf_oracle.c).
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "libfaac/sbr.h"
#include "libfaac/sbr_internal.h"
#include "libfaac/sbr_tables.h"
#include "qmf_oracle.h"

#define SLOT   32
#define BANDS  32
#define FS     48000
#define NSLOTS 1000

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static SBRInfo *make_sbr(void)
{
    SBRInfo *s = SBRInit(1, FS, FS / 2, 64000);
    if (!s) { fprintf(stderr, "SBRInit failed\n"); exit(2); }
    return s;
}

static double get_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* Accuracy and Benchmarking for 64-band energy kernel */
static void run_case_64(const char *name, const faac_real *input, int n_slots)
{
    SBRInfo *sbr = make_sbr();
    int kx = sbr->kx, k2 = sbr->k2;
    faac_real state[640] = {0};
    double ref_acc = 0, worst = -1e9;
    int worst_k = -1;
    double band_err[64] = {0};
    float ref_state[640] = {0};

    /* Accuracy pass: FFT */
    for (int s = 0; s < n_slots; s++) {
        faac_real energy[64];
        qmf_analysis_64_slot_energy_test(sbr, input + s * 64, state, energy, kx, k2);

        float ref_in[64];
        float ref_en[64];
        for (int i = 0; i < 64; i++) ref_in[i] = (float)input[s * 64 + i];
        qmf_ref_64_slot_energy(ref_in, ref_state, ref_en);

        for (int k = kx; k < k2; k++) {
            double Eref = (double)ref_en[k];
            double d = (double)energy[k] - Eref;
            band_err[k] += d * d;
            ref_acc += Eref;
        }
    }

    double ref_mean = ref_acc / ((k2 - kx) * n_slots) + 1e-30;
    for (int k = kx; k < k2; k++) {
        double rms_err = sqrt(band_err[k] / n_slots);
        double db = 20.0 * log10(rms_err / (ref_mean + 1e-30) + 1e-30);
        if (db > worst) { worst = db; worst_k = k; }
    }

    /* Accuracy pass: Direct */
    double worst_dir = -1e9;
    int worst_k_dir = -1;
    double band_err_dir[64] = {0};
    memset(state, 0, sizeof(state));
    memset(ref_state, 0, sizeof(ref_state));
    for (int s = 0; s < n_slots; s++) {
        faac_real energy[64];
        qmf_analysis_64_slot_energy_direct_test(sbr, input + s * 64, state, energy, kx, k2);
        float ref_in[64];
        float ref_en[64];
        for (int i = 0; i < 64; i++) ref_in[i] = (float)input[s * 64 + i];
        qmf_ref_64_slot_energy(ref_in, ref_state, ref_en);
        for (int k = kx; k < k2; k++) {
            double d = (double)energy[k] - (double)ref_en[k];
            band_err_dir[k] += d * d;
        }
    }
    for (int k = kx; k < k2; k++) {
        double db = 20.0 * log10(sqrt(band_err_dir[k] / n_slots) / (ref_mean + 1e-30) + 1e-30);
        if (db > worst_dir) { worst_dir = db; worst_k_dir = k; }
    }

    /* Benchmarking pass: FFT */
    double t0 = get_time();
    int bench_iters = 100;
    for (int b = 0; b < bench_iters; b++) {
        memset(state, 0, sizeof(state));
        for (int s = 0; s < n_slots; s++) {
            faac_real energy[64];
            qmf_analysis_64_slot_energy_test(sbr, input + s * 64, state, energy, kx, k2);
        }
    }
    double t1 = get_time();
    double speed_fft = ((double)n_slots * bench_iters) / (t1 - t0);

    /* Benchmarking pass: Direct */
    t0 = get_time();
    for (int b = 0; b < bench_iters; b++) {
        memset(state, 0, sizeof(state));
        for (int s = 0; s < n_slots; s++) {
            faac_real energy[64];
            qmf_analysis_64_slot_energy_direct_test(sbr, input + s * 64, state, energy, kx, k2);
        }
    }
    t1 = get_time();
    double speed_dir = ((double)n_slots * bench_iters) / (t1 - t0);

    printf("  [64-band %s] FFT: err=%+7.2f dB, speed=%8.0f | Direct: err=%+7.2f dB, speed=%8.0f\n",
           name, worst, speed_fft, worst_dir, speed_dir);

    SBREnd(sbr);
}

static faac_real *impulse_buf(int n_slots, int stride)
{
    faac_real *b = calloc(n_slots * stride, sizeof(faac_real));
    b[0] = 1.0f;
    return b;
}

static faac_real *tone_buf(int n_slots, int stride, double cycles_per_sample)
{
    int N = n_slots * stride;
    faac_real *b = malloc(N * sizeof(faac_real));
    for (int n = 0; n < N; n++)
        b[n] = (faac_real)sin(2.0 * M_PI * cycles_per_sample * n);
    return b;
}

static faac_real *dc_buf(int n_slots, int stride)
{
    int N = n_slots * stride;
    faac_real *b = malloc(N * sizeof(faac_real));
    for (int n = 0; n < N; n++) b[n] = 1.0f;
    return b;
}

static faac_real *nyquist_buf(int n_slots, int stride)
{
    int N = n_slots * stride;
    faac_real *b = malloc(N * sizeof(faac_real));
    for (int n = 0; n < N; n++) b[n] = (n & 1) ? -1.0f : 1.0f;
    return b;
}

int main(void)
{
    printf("QMF hardened test & benchmark\n");

    struct {
        const char *name;
        faac_real *(*func)(int, int);
    } cases[] = {
        {"impulse", impulse_buf},
        {"dc", dc_buf},
        {"nyquist", nyquist_buf},
    };

    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        faac_real *b = cases[i].func(NSLOTS, 64);
        run_case_64(cases[i].name, b, NSLOTS);
        free(b);
    }

    /* Tone cases */
    double freqs[] = {0.05, 0.25, 0.45};
    for (size_t i = 0; i < sizeof(freqs)/sizeof(freqs[0]); i++) {
        char name[32]; snprintf(name, sizeof(name), "tone f=%.2f", freqs[i]);
        faac_real *b = tone_buf(NSLOTS, 64, freqs[i]);
        run_case_64(name, b, NSLOTS);
        free(b);
    }

    return 0;
}
