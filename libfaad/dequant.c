/*
 * Inverse Quantization and PNS
 */

#include "faad_internal.h"

static float pow_4_3_lut[33];
static float sf_scale_lut[256];
static bool dequant_tables_init = false;

static void init_dequant_tables(void)
{
    if (dequant_tables_init) return;

    for (int i = 0; i <= 32; i++) {
        pow_4_3_lut[i] = powf((float)i, 4.0f / 3.0f);
    }
    for (int i = 0; i < 256; i++) {
        sf_scale_lut[i] = powf(2.0f, 0.25f * (i - 100));
    }

    dequant_tables_init = true;
}

static inline float pow_4_3_fast(int x)
{
    int abs_x = abs(x);
    if (abs_x <= 32) {
        float val = pow_4_3_lut[abs_x];
        return (x < 0) ? -val : val;
    }
    float val = powf((float)abs_x, 4.0f / 3.0f);
    return (x < 0) ? -val : val;
}

void dequantize_spectrum(ICSInfo *ics, float *spec)
{
    init_dequant_tables();

    for (int g = 0; g < ics->num_window_groups; g++) {
        for (int i = 0; i < ics->num_sections[g]; i++) {
            int cb = ics->sect_cb[g][i];
            if (cb == 0 || cb == 13) continue;

            int start_sfb = ics->sect_start[g][i];
            int end_sfb = ics->sect_end[g][i];

            for (int sfb = start_sfb; sfb < end_sfb; sfb++) {
                int sf = ics->scalefactors[g][sfb];
                float scale = (sf >= 0 && sf < 256) ? sf_scale_lut[sf] : powf(2.0f, 0.25f * (sf - 100));

                int start_k = ics->sfb_offsets[sfb];
                int end_k = ics->sfb_offsets[sfb + 1];

                for (int w = 0; w < ics->window_group_length[g]; w++) {
                    float *ptr = spec + w * 128 + start_k;
                    for (int k = start_k; k < end_k; k++) {
                        int val = (int)(*ptr);
                        if (val != 0) {
                            *ptr = pow_4_3_fast(val) * scale;
                        }
                        ptr++;
                    }
                }
            }
        }
    }
}

void apply_pns(ICSInfo *ics, float *spec, uint32_t *pns_seed)
{
    init_dequant_tables();

    for (int g = 0; g < ics->num_window_groups; g++) {
        for (int sfb = 0; sfb < ics->num_sfbs; sfb++) {
            if (ics->pns_used[g][sfb]) {
                int sf = ics->scalefactors[g][sfb];
                float scale = (sf >= 0 && sf < 256) ? sf_scale_lut[sf] : powf(2.0f, 0.25f * (sf - 100));

                int start_k = ics->sfb_offsets[sfb];
                int end_k = ics->sfb_offsets[sfb + 1];
                int len = end_k - start_k;

                for (int w = 0; w < ics->window_group_length[g]; w++) {
                    float *ptr = spec + w * 128 + start_k;
                    float energy = 0.0f;

                    for (int k = 0; k < len; k++) {
                        *pns_seed = (*pns_seed * 1664525U) + 1013904223U;
                        float noise = ((float)(int32_t)*pns_seed) / 2147483648.0f;
                        ptr[k] = noise;
                        energy += noise * noise;
                    }

                    if (energy > 0.0f) {
                        float norm = scale / sqrtf(energy);
                        for (int k = 0; k < len; k++) {
                            ptr[k] *= norm;
                        }
                    }
                }
            }
        }
    }
}
