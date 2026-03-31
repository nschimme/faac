#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "quantize.h"
#include "huff2.h"
#include "cpu_compute.h"
#include "util.h"

static void q_scalar(const faac_real *xr, int *xi, int n, faac_real f) {
    for (int i = 0; i < n; i++) {
        faac_real v = xr[i], t = FAAC_FABS(v) * f; t = FAAC_POW(t, 0.75);
        int q = (int)(t + 0.4054); if (q > 8191) q = 8191; xi[i] = (v < 0) ? -q : q;
    }
}
#if defined(HAVE_SSE2)
extern void quantize_sse2(const faac_real *xr, int *xi, int n, faac_real f);
#endif
void QuantizeInit(void) {}

static void count_bits_loop(CoderInfo *ci, int sfb, int trial_sfac, float *noise, int *bits) {
    if (sfb < 0 || sfb >= MAX_SCFAC_BANDS) { *noise = 1e10f; *bits = 1000000; return; }
    int idx = trial_sfac + 155;
    if (idx >= 0 && idx < 256 && ci->quantCacheValid[sfb][idx]) {
        *noise = ci->quantCache[sfb][idx].noise; *bits = ci->quantCache[sfb][idx].bits; return;
    }
    int start = ci->sfb_offset[sfb], end = ci->sfb_offset[sfb+1], n = end - start;
    if (n <= 0) { *noise = 0; *bits = 0; return; }
    faac_real f = (faac_real)pow(2.0, 0.25 * (trial_sfac - 100));
    int xi[BLOCK_LEN_LONG]; if (n > BLOCK_LEN_LONG) n = BLOCK_LEN_LONG;
#if defined(HAVE_SSE2)
    if (get_cpu_caps() & CPU_CAP_SSE2) quantize_sse2(ci->xabs + start, xi, n, f); else q_scalar(ci->xabs + start, xi, n, f);
#else
    q_scalar(ci->xabs + start, xi, n, f);
#endif
    *bits = huff_count_bits(xi, n, NULL);
    *noise = 0; faac_real invf = 1.0 / f;
    for (int i = 0; i < n; i++) {
        faac_real x = ci->xabs[start + i];
        faac_real iq = (faac_real)(pow((double)abs(xi[i]), 4.0/3.0) * (double)invf);
        faac_real d = x - iq; *noise += (float)(d * d);
    }
    if (idx >= 0 && idx < 256) { ci->quantCache[sfb][idx].bits = *bits; ci->quantCache[sfb][idx].noise = *noise; ci->quantCacheValid[sfb][idx] = 1; }
}

void twoloop_exec(CoderInfo *ci, float *thr, int target) {
    int sfac_offsets[MAX_SCFAC_BANDS]; int bits, total, best_mid; float noise;
    int is_spectral[MAX_SCFAC_BANDS];
    int fixed_bits = 0;

    if (ci->sfbn > MAX_SCFAC_BANDS) ci->sfbn = MAX_SCFAC_BANDS;

    for (int i = 0; i < ci->sfbn; i++) {
        sfac_offsets[i] = 0;
        if (ci->book[i] == HCB_PNS || ci->book[i] == HCB_INTENSITY || ci->book[i] == HCB_INTENSITY2) {
            is_spectral[i] = 0;
            if (ci->book[i] == HCB_PNS) fixed_bits += 9;
            else fixed_bits += 6; // conservative estimate for IS diff SF
        } else {
            is_spectral[i] = 1;
            ci->book[i] = HCB_NONE;
        }
    }

    int target_spectral = (target * 80) / 100 - fixed_bits;
    if (target_spectral < 100) target_spectral = 100;

    best_mid = 100;
    for (int iter = 0; iter < 10; iter++) {
        int low = -155, high = 100, best = -155;
        while (low <= high) {
            int mid = (low + high) / 2; total = 0;
            for (int i = 0; i < ci->sfbn; i++) {
                if (!is_spectral[i]) continue;
                int s = mid + sfac_offsets[i]; if (s < -155) s = -155; if (s > 100) s = 100;
                count_bits_loop(ci, i, s, &noise, &bits); total += bits;
            }
            if (total <= target_spectral) { best = mid; low = mid + 1; } else high = mid - 1;
        }
        if (best != -155) best_mid = best;
        int over = 0;
        for (int i = 0; i < ci->sfbn; i++) {
            if (!is_spectral[i]) continue;
            int s = best_mid + sfac_offsets[i]; if (s < -155) s = -155; if (s > 100) s = 100;
            count_bits_loop(ci, i, s, &noise, &bits); if (noise > thr[i] && sfac_offsets[i] < 50) { sfac_offsets[i]++; over++; }
        }
        if (!over) break;
    }
    ci->bandcnt = 0;
    for (int i = 0; i < ci->sfbn; i++) {
        if (!is_spectral[i]) { ci->bandcnt++; continue; }
        int s = best_mid + sfac_offsets[i]; if (s < -155) s = -155; if (s > 100) s = 100;
        int st = ci->sfb_offset[i], n = ci->sfb_offset[i+1] - st;
        faac_real f = (faac_real)pow(2.0, 0.25 * (s - 100));
#if defined(HAVE_SSE2)
        if (get_cpu_caps() & CPU_CAP_SSE2) quantize_sse2(ci->xabs + st, ci->xitab + st, n, f); else q_scalar(ci->xabs + st, ci->xitab + st, n, f);
#else
        q_scalar(ci->xabs + st, ci->xitab + st, n, f);
#endif
        huffbook(ci, ci->xitab + st, n); ci->sf[i] = 100 - s;
    }
}

int BlocQuant(CoderInfo *ci, faac_real *xr, AACQuantCfg *cfg) {
    float thr[MAX_SCFAC_BANDS]; memset(ci->quantCacheValid, 0, sizeof(ci->quantCacheValid)); ci->datacnt = 0;
    for (int i = 0; i < FRAME_LEN; i++) ci->xabs[i] = FAAC_FABS(xr[i]);
    if (ci->sfbn > MAX_SCFAC_BANDS) ci->sfbn = MAX_SCFAC_BANDS;
    for (int i = 0; i < ci->sfbn; i++) {
        int st = ci->sfb_offset[i], en = ci->sfb_offset[i+1]; float e = 0;
        for (int j = st; j < en; j++) e += (float)(ci->xabs[j] * ci->xabs[j]);
        thr[i] = e * 0.0012f * (100.0f / (cfg->quality > 0 ? cfg->quality : 1));
    }
    twoloop_exec(ci, thr, (int)cfg->target_bits ? (int)cfg->target_bits : 3000);
    int first = -1; for (int i = 0; i < ci->sfbn; i++) { if (ci->book[i] > 0 && ci->book[i] < 13) { first = i; break; } }
    if (first >= 0) {
        ci->global_gain = ci->sf[first]; int lastsf = ci->global_gain;
        for (int i = first; i < ci->sfbn; i++) {
            if (ci->book[i] > 0 && ci->book[i] < 13) {
                int d = ci->sf[i] - lastsf; if (d < -60) d = -60; if (d > 60) d = 60;
                lastsf += d; ci->sf[i] = lastsf;
            }
        }
    } else { ci->global_gain = 0; }
    return 1;
}

void CalcBW(unsigned *bw, int rate, SR_INFO *sr, AACQuantCfg *cfg) {
    int max = *bw * 2048 / rate, l = 0, cnt;
    for (cnt = 0; cnt < sr->num_cb_long; cnt++) { if (l >= max) break; l += sr->cb_width_long[cnt]; }
    cfg->max_cbl = cnt; cfg->max_l = l; *bw = (faac_real)l * rate / 2048;
}
void BlocGroup(faac_real *xr, CoderInfo *ci, AACQuantCfg *cfg) {}
void BlocStat(void) {}
