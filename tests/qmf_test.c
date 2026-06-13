/* QMF analysis harness: compares production qmf_analysis_slot_complex
 * against the standalone oracle (tests/qmf_oracle.c).
 *
 * Pass criterion per the plan: per-band |X[k]|² matches oracle within
 * −40 dBFS relative error.  Until Phase C rewrites production, this test
 * is EXPECTED to fail; running it prints the worst-case gap so progress
 * is measurable. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libfaac/sbr.h"
#include "libfaac/sbr_internal.h"
#include "libfaac/sbr_tables.h"
#include "qmf_oracle.h"

#define SLOT   32
#define BANDS  32
#define FS     48000
#define NSLOTS 100

static SBRInfo *make_sbr(void)
{
    /* coreSampleRate arg is unused by the QMF itself; pick Fs/2.
     * bitRate arg is also unused for the QMF math, using 64k placeholder. */
    SBRInfo *s = SBRInit(1, FS, FS / 2, 64000);
    if (!s) { fprintf(stderr, "SBRInit failed\n"); exit(2); }
    return s;
}

/* Drive N_slots of input through both oracle and production; return max
 * per-band relative energy error in dB (−∞ = perfect match). */
static double run_case(const char *name,
                       const faac_real *input,
                       int n_slots)
{
    SBRInfo *sbr = make_sbr();
    float ostate[320] = {0};
    double band_ref_acc[BANDS] = {0};
    double band_err_acc[BANDS] = {0};

    for (int s = 0; s < n_slots; s++) {
        float ore[BANDS], oim[BANDS];
        float oin[SLOT];
        for (int i = 0; i < SLOT; i++) oin[i] = (float)input[s * SLOT + i];
        qmf_ref_slot(oin, ostate, ore, oim);

        faac_real pre[BANDS], pim[BANDS];
        qmf_analysis_slot_complex(sbr,
                                  input + s * SLOT,
                                  sbr->qmfOvl[0],
                                  pre, pim);

        for (int k = 0; k < BANDS; k++) {
            double Eo = (double)ore[k] * ore[k] + (double)oim[k] * oim[k];
            double Ep = (double)pre[k] * pre[k] + (double)pim[k] * pim[k];
            band_ref_acc[k] += Eo;
            double d = Ep - Eo;
            band_err_acc[k] += d * d;
        }
    }

    double worst_db = -1e9;
    int worst_k = -1;
    double total_ref = 0;
    for (int k = 0; k < BANDS; k++) total_ref += band_ref_acc[k];
    double ref_mean = total_ref / BANDS + 1e-30;

    for (int k = 0; k < BANDS; k++) {
        double rms_err = sqrt(band_err_acc[k] / n_slots);
        double db = 20.0 * log10(rms_err / sqrt(ref_mean) + 1e-30);
        if (db > worst_db) { worst_db = db; worst_k = k; }
    }
    printf("  [%s] worst-band err = %+7.2f dB (band %d)\n",
           name, worst_db, worst_k);

    SBREnd(sbr);
    return worst_db;
}

static faac_real *impulse_buf(int n_slots)
{
    faac_real *b = calloc(n_slots * SLOT, sizeof(faac_real));
    b[0] = 1.0f;
    return b;
}

static faac_real *tone_buf(int n_slots, double cycles_per_sample)
{
    int N = n_slots * SLOT;
    faac_real *b = malloc(N * sizeof(faac_real));
    for (int n = 0; n < N; n++)
        b[n] = (faac_real)sin(2.0 * M_PI * cycles_per_sample * n);
    return b;
}

static faac_real *noise_buf(int n_slots)
{
    int N = n_slots * SLOT;
    faac_real *b = malloc(N * sizeof(faac_real));
    unsigned s = 0x12345u;
    for (int n = 0; n < N; n++) {
        s = s * 1103515245u + 12345u;
        double u1 = ((s >> 8) & 0xffff) / 65536.0 + 1e-9;
        s = s * 1103515245u + 12345u;
        double u2 = ((s >> 8) & 0xffff) / 65536.0;
        b[n] = (faac_real)(sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2));
    }
    return b;
}

/* Reference for the 64-band analysis: same 640-tap polyphase window, but
 * direct (slow) modulation with libm trig instead of the production FFT
 * factorization. Returns worst-band energy error in dB across n_slots of
 * 64 samples. */
static double run_case_64(const char *name, const faac_real *input, int n_slots)
{
    SBRInfo *sbr = make_sbr();
    int kx = sbr->kx, k2 = sbr->k2;
    faac_real refovl[640] = {0};
    double ref_acc = 0, worst = -1e9;
    int worst_k = -1;
    double band_err[64] = {0};

    for (int s = 0; s < n_slots; s++) {
        faac_real energy[64];
        qmf_analysis_64_slot_energy_test(sbr, input + s * 64, sbr->qmfOvl64[0], energy, kx, k2);

        memmove(refovl, refovl + 64, (640 - 64) * sizeof(faac_real));
        memcpy(refovl + 640 - 64, input + s * 64, 64 * sizeof(faac_real));
        double un[128];
        for (int n = 0; n < 128; n++)
            un[n] = (double)sbr_qmf_window_us640[n] * refovl[639 - n] +
                    (double)sbr_qmf_window_us640[n + 128] * refovl[511 - n] +
                    (double)sbr_qmf_window_us640[n + 256] * refovl[383 - n] +
                    (double)sbr_qmf_window_us640[n + 384] * refovl[255 - n] +
                    (double)sbr_qmf_window_us640[n + 512] * refovl[127 - n];
        for (int k = kx; k < k2; k++) {
            double re = 0, im = 0;
            for (int n = 0; n < 128; n++) {
                double ph = M_PI * (2 * k + 1) * (2 * n - 127) / 256.0;
                re += un[n] * cos(ph);
                im += un[n] * sin(ph);
            }
            double Eref = re * re + im * im;
            double d = (double)energy[k] - Eref;
            band_err[k] += d * d;
            ref_acc += Eref;
        }
    }
    double ref_mean = ref_acc / ((k2 - kx) * n_slots) + 1e-30;
    for (int k = kx; k < k2; k++) {
        double db = 20.0 * log10(sqrt(band_err[k] / n_slots) / (ref_mean + 1e-30) + 1e-30);
        if (db > worst) { worst = db; worst_k = k; }
    }
    printf("  [%s] worst-band err = %+7.2f dB (band %d)\n", name, worst, worst_k);
    SBREnd(sbr);
    return worst;
}

int main(void)
{
    double threshold_db = -40.0;
    int failed = 0;

    printf("QMF oracle-vs-production harness (threshold %.1f dB)\n",
           threshold_db);

    {
        faac_real *b = impulse_buf(4);
        double w = run_case("impulse", b, 4);
        if (w > threshold_db) failed++;
        free(b);
    }
    {
        /* Band centre k lies at normalised freq (k + 0.5)/64. */
        int ks[] = { 5, 15, 25 };
        for (size_t i = 0; i < sizeof(ks)/sizeof(ks[0]); i++) {
            int k = ks[i];
            char name[32]; snprintf(name, sizeof(name), "tone k=%d", k);
            faac_real *b = tone_buf(NSLOTS, (k + 0.5) / 64.0);
            double w = run_case(name, b, NSLOTS);
            if (w > threshold_db) failed++;
            free(b);
        }
    }
    {
        faac_real *b = noise_buf(NSLOTS);
        double w = run_case("white noise", b, NSLOTS);
        if (w > threshold_db) failed++;
        free(b);
    }

    /* 64-band FFT analysis path vs direct modulation reference. The 64-band
     * path consumes 64 full-rate samples per slot; band k centre is at
     * (k + 0.5)/128 cycles/sample. */
    {
        int ks[] = { 35, 45 };
        for (size_t i = 0; i < sizeof(ks)/sizeof(ks[0]); i++) {
            char name[40]; snprintf(name, sizeof(name), "64-band tone k=%d", ks[i]);
            faac_real *b = tone_buf(NSLOTS * 2, (ks[i] + 0.5) / 128.0);
            double w = run_case_64(name, b, NSLOTS);
            if (w > threshold_db) failed++;
            free(b);
        }
        faac_real *b = noise_buf(NSLOTS * 2);
        double w = run_case_64("64-band noise", b, NSLOTS);
        if (w > threshold_db) failed++;
        free(b);
    }

    if (failed) {
        printf("FAIL: %d case(s) above threshold\n", failed);
        return 1;
    }
    printf("PASS\n");
    return 0;
}
