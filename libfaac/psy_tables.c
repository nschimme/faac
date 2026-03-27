#include <math.h>
#include <string.h>
#include "psy_tables.h"
#include "util.h"

const ATHTableEntry iso226_ath_table[ATH_TABLE_SIZE] = {
    {17, 83.0}, {18, 81.0}, {20, 78.5}, {21.2, 75.0}, {22.4, 73.0}, {25, 68.7}, {26.5, 66.5}, {28, 64.0}, {31.5, 59.5}, {33.5, 57.5},
    {35.5, 55.0}, {40, 51.1}, {42.5, 49.0}, {45, 47.0}, {50, 44.0}, {53, 42.0}, {56, 40.5}, {63, 37.5}, {67, 36.0}, {71, 34.5},
    {80, 31.5}, {85, 30.5}, {90, 29.0}, {100, 26.5}, {106, 25.0}, {112, 24.0}, {125, 22.1}, {132, 21.0}, {140, 20.0}, {160, 17.9},
    {170, 17.0}, {180, 16.0}, {200, 14.4}, {212, 13.5}, {224, 13.0}, {250, 11.4}, {265, 10.5}, {280, 10.0}, {315, 8.6}, {335, 8.0},
    {355, 7.5}, {400, 6.2}, {425, 5.5}, {450, 5.0}, {500, 4.4}, {530, 4.0}, {560, 3.5}, {630, 3.0}, {670, 2.8}, {710, 2.5},
    {800, 2.2}, {850, 2.2}, {900, 2.2}, {1000, 2.4}, {1060, 3.3}, {1120, 3.5}, {1250, 3.5}, {1320, 3.0}, {1400, 2.5}, {1600, 1.7},
    {1700, 1.0}, {1800, 0.0}, {2000, -1.3}, {2120, -2.0}, {2650, -5.0}, {2800, -5.5}, {3150, -6.5}, {3350, -7.5},
    {3550, -8.0}, {4000, -9.3}, {4250, -9.2}, {4500, -9.0}, {5000, -8.1}, {5300, -6.5}, {5600, -5.0}, {6300, -1.1}, {6700, 2.0}, {7100, 5.0},
    {8000, 15.3}, {8500, 15.5}, {9000, 16.0}, {10000, 16.4}, {10600, 15.5}, {11200, 14.0}, {12500, 11.6}, {13200, 12.0}, {14000, 13.0}, {16000, 20.9}
};

static faac_real freq_to_bark(faac_real freq) {
    return 13.3 * atan(0.00076 * freq) + 3.5 * atan(pow(freq / 7500.0, 2));
}

static faac_real interpolate_ath(faac_real freq) {
    int i;
    if (freq <= iso226_ath_table[0].freq) return iso226_ath_table[0].db;
    for (i = 0; i < ATH_TABLE_SIZE - 1; i++) {
        if (freq >= iso226_ath_table[i].freq && freq <= iso226_ath_table[i+1].freq) {
            faac_real frac = (freq - iso226_ath_table[i].freq) / (iso226_ath_table[i+1].freq - iso226_ath_table[i].freq);
            return iso226_ath_table[i].db + frac * (iso226_ath_table[i+1].db - iso226_ath_table[i].db);
        }
    }
    return iso226_ath_table[ATH_TABLE_SIZE-1].db;
}

static void compute_spreading_matrix(faac_real *matrix, faac_real *bark, int n, int stride) {
    int i, j;
    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            faac_real dz = bark[i] - bark[j];
            faac_real dz_off = dz + 0.474;
            faac_real weight = 15.811389 + 7.5 * dz_off - 17.5 * sqrt(1.0 + dz_off * dz_off);
            /* Apply a 18dB masking index (SNR) offset to convert masking function to threshold */
            weight -= 18.0;
            if (weight < -100.0) weight = -100.0;
            matrix[i * stride + j] = pow(10.0, weight / 10.0);
        }
    }
}

void FaacPsyInitContext(FaacPsyContext *ctx, int sample_rate,
                        int *cb_width_long, int num_cb_long,
                        int *cb_width_short, int num_cb_short) {
    int sfb;
    int offset = 0;

    memset(ctx, 0, sizeof(FaacPsyContext));

    for (sfb = 0; sfb < num_cb_long; sfb++) {
        faac_real center_freq = (faac_real)(offset + cb_width_long[sfb] / 2) * sample_rate / (2.0 * BLOCK_LEN_LONG);
        ctx->bark_long[sfb] = freq_to_bark(center_freq);
        ctx->ath_long[sfb] = pow(10.0, (interpolate_ath(center_freq) - 90.0) / 10.0) * (cb_width_long[sfb] * 0.1);
        offset += cb_width_long[sfb];
    }
    compute_spreading_matrix(ctx->spread_matrix_long, ctx->bark_long, num_cb_long, NSFB_LONG_H);

    offset = 0;
    for (sfb = 0; sfb < num_cb_short; sfb++) {
        faac_real center_freq = (faac_real)(offset + cb_width_short[sfb] / 2) * sample_rate / (2.0 * BLOCK_LEN_SHORT);
        ctx->bark_short[sfb] = freq_to_bark(center_freq);
        ctx->ath_short[sfb] = pow(10.0, (interpolate_ath(center_freq) - 90.0) / 10.0) * (cb_width_short[sfb] * 0.1);
        offset += cb_width_short[sfb];
    }
    compute_spreading_matrix(ctx->spread_matrix_short, ctx->bark_short, num_cb_short, NSFB_SHORT_H);
}
