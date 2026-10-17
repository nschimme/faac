/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2026 Nils Schimmelmann
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */

/* Unit test verifying build_freq_table against ISO 14496-3 §4.6.18.3.2.1. */

#include "sbr.h"
#include "sbr_internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cmp_int(const void *a, const void *b) {
    return *(const int *)a - *(const int *)b;
}

static int sbr_round(double v) {
    return (int)floor(v + 0.5);
}

/* Double-precision reference transcription of ISO 14496-3 §4.6.18.3.2.1. */
static int ref_build_log_band_widths(int k_start, int k_end, int bands_per_octave,
                                      double warp, int *vdk) {
    int nb = 2 * sbr_round(bands_per_octave * log2((double)k_end / (double)k_start) / (2.0 * warp));
    if (nb < 1) nb = 1;
    if (nb > SBR_MAX_BANDS) nb = SBR_MAX_BANDS;
    int prev = k_start;
    for (int i = 0; i < nb; i++) {
        int cur = sbr_round(k_start * pow((double)k_end / (double)k_start, (double)(i + 1) / (double)nb));
        vdk[i] = cur - prev;
        prev = cur;
    }
    qsort(vdk, nb, sizeof(int), cmp_int);
    return nb;
}

static int ref_build_freq_table(int kx, int k2, int bs_freq_scale, int bs_alter_scale, int *edges) {
    int bands_per_octave = 14 - 2 * bs_freq_scale;
    int n_master;

    if ((double)k2 / (double)kx > 2.2449) {
        int k1 = 2 * kx;
        int dk0[SBR_MAX_BANDS], dk1[SBR_MAX_BANDS];
        int nb0 = ref_build_log_band_widths(kx, k1, bands_per_octave, 1.0, dk0);
        double warp = bs_alter_scale ? 1.3 : 1.0;
        int nb1 = ref_build_log_band_widths(k1, k2, bands_per_octave, warp, dk1);

        if (dk1[0] < dk0[nb0 - 1]) {
            int change = dk0[nb0 - 1] - dk1[0];
            int half = (dk1[nb1 - 1] - dk1[0]) / 2;
            if (change > half) change = half;
            dk1[0] += change;
            dk1[nb1 - 1] -= change;
            qsort(dk1, nb1, sizeof(int), cmp_int);
        }

        n_master = nb0 + nb1;
        edges[0] = kx;
        int k = 0;
        for (int i = 0; i < nb0; i++, k++) edges[k + 1] = edges[k] + dk0[i];
        for (int i = 0; i < nb1; i++, k++) edges[k + 1] = edges[k] + dk1[i];
    } else {
        int dk0[SBR_MAX_BANDS];
        n_master = ref_build_log_band_widths(kx, k2, bands_per_octave, 1.0, dk0);
        edges[0] = kx;
        for (int i = 0; i < n_master; i++) edges[i + 1] = edges[i] + dk0[i];
    }
    return n_master;
}

int main(void) {
    int total = 0, two_region_count = 0, mismatches = 0;

    for (int kx = 12; kx <= 60; kx++) {
        for (int k2 = kx + 4; k2 <= 64; k2++) {
            for (int fs = 1; fs <= 3; fs++) {
                for (int alt = 0; alt <= 1; alt++) {
                    /* SBRInfo has flexible array member ch[], allocate sufficient room */
                    SBRInfo *sbr = (SBRInfo *)calloc(1, sizeof(SBRInfo) + 2 * sizeof(SBRChannel));
                    sbr->kx = kx;
                    sbr->k2 = k2;
                    sbr->bs_freq_scale = fs;
                    sbr->bs_alter_scale = alt;

                    /* Build via libfaac function */
                    build_freq_table(sbr);
                    int actual_n = sbr->numBands;
                    int actual_edges[SBR_MAX_BANDS + 1];
                    memcpy(actual_edges, sbr->bandEdges, (actual_n + 1) * sizeof(int));
                    free(sbr);

                    /* Build via reference ISO function */
                    int ref_edges[SBR_MAX_BANDS + 1];
                    int ref_n = ref_build_freq_table(kx, k2, fs, alt, ref_edges);

                    total++;
                    if ((double)k2 / (double)kx > 2.2449) two_region_count++;

                    if (actual_n != ref_n || memcmp(actual_edges, ref_edges, (ref_n + 1) * sizeof(int)) != 0) {
                        mismatches++;
                        fprintf(stderr, "Mismatch at kx=%d k2=%d fs=%d alt=%d: actual_n=%d ref_n=%d\n",
                                kx, k2, fs, alt, actual_n, ref_n);
                    }
                }
            }
        }
    }

    printf("Checked %d SBR master tables (%d two-region): %d mismatches\n",
           total, two_region_count, mismatches);

    return mismatches == 0 ? 0 : 1;
}
