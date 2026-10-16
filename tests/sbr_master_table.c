/*
 * FAAC - Freeware Advanced Audio Coder
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

/* The SBR master frequency table (ISO/IEC 14496-3 §4.6.18.3.2.1) is written
 * nowhere in the stream: encoder and decoder each derive it from kx, k2,
 * bs_freq_scale and bs_alter_scale, so any difference puts every envelope in
 * the wrong band. This checks build_freq_table() against a direct, double
 * precision transcription of the spec over every combination the encoder can
 * reach, including the two-region case above k2/kx = 2.2449. */

#include "sbr.c"

#include <stdio.h>

static int ref_round(double v) { return (int)floor(v + 0.5); }

static int cmp_i(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

/* Widths of the bands between a0 and a1: nb = 2 * round(bpo / 2 * log2(a1 / a0) / warp)
 * bands, each the difference of two rounded points of a geometric series. */
static int ref_widths(int a0, int a1, int bpo, double warp, int *dk)
{
    int nb = 2 * ref_round(bpo / 2.0 * log2((double)a1 / a0) / warp);
    int prev = a0;

    for (int i = 0; i < nb; i++) {
        int cur = ref_round(a0 * pow((double)a1 / a0, (double)(i + 1) / nb));
        dk[i] = cur - prev;
        prev = cur;
    }
    qsort(dk, nb, sizeof(int), cmp_i);
    return nb;
}

static int ref_master(int kx, int k2, int bpo, int alter, int *edges)
{
    int dk0[128], dk1[128], n, nb0, nb1 = 0;
    int k1 = ((double)k2 / kx > 2.2449) ? 2 * kx : k2;

    nb0 = ref_widths(kx, k1, bpo, 1.0, dk0);
    if (k1 != k2) {
        nb1 = ref_widths(k1, k2, bpo, alter ? 1.3 : 1.0, dk1);
        if (dk1[0] < dk0[nb0 - 1]) {
            int change = dk0[nb0 - 1] - dk1[0];
            int half = (dk1[nb1 - 1] - dk1[0]) / 2;
            if (change > half) change = half;
            dk1[0] += change;
            dk1[nb1 - 1] -= change;
            qsort(dk1, nb1, sizeof(int), cmp_i);
        }
    }
    n = nb0 + nb1;
    edges[0] = kx;
    for (int i = 0; i < nb0; i++) edges[i + 1] = edges[i] + dk0[i];
    for (int i = 0; i < nb1; i++) edges[nb0 + i + 1] = edges[nb0 + i] + dk1[i];
    return n;
}

int main(void)
{
    static SBRInfo sbr;
    int failures = 0, cases = 0, two_region = 0;

    for (int kx = 8; kx <= 48; kx++) {
        for (int k2 = kx + SBR_MIN_SPAN; k2 <= 64 && k2 - kx <= 48; k2++) {
            for (int fs = 1; fs <= 3; fs++) {
                for (int alter = 0; alter <= 1; alter++) {
                    int ref[130], n_ref, bpo = 14 - 2 * fs;

                    n_ref = ref_master(kx, k2, bpo, alter, ref);
                    if (n_ref > SBR_MAX_BANDS)
                        continue; /* not representable; the encoder never asks for it */
                    int valid = 1; /* a band must span at least one QMF channel */
                    for (int i = 1; i <= n_ref; i++)
                        if (ref[i] <= ref[i - 1]) valid = 0;
                    if (!valid || ref[n_ref] != k2)
                        continue; /* no stream can carry this table */

                    memset(&sbr, 0, sizeof(sbr));
                    sbr.kx = kx;
                    sbr.k2 = k2;
                    sbr.bs_freq_scale = fs;
                    sbr.bs_alter_scale = alter;
                    build_freq_table(&sbr);

                    cases++;
                    two_region += ((double)k2 / kx > 2.2449);

                    int bad = (sbr.numBands != n_ref) || sbr.bandEdges[sbr.numBands] != k2;
                    for (int i = 0; !bad && i <= n_ref; i++) {
                        if (sbr.bandEdges[i] != ref[i]) bad = 1;
                        if (i > 0 && sbr.bandEdges[i] <= sbr.bandEdges[i - 1]) bad = 1;
                    }
                    if (bad && failures++ < 10)
                        fprintf(stderr, "FAIL kx=%d k2=%d freq_scale=%d alter_scale=%d: %d bands, expected %d\n",
                                kx, k2, fs, alter, sbr.numBands, n_ref);
                }
            }
        }
    }

    printf("sbr master table: %d cases (%d two-region), %d failures\n", cases, two_region, failures);
    return failures ? 1 : 0;
}
