/*
 * Mid/Side and Intensity Stereo Decoding
 */

#include "faad_internal.h"

void apply_ms_stereo(CPEInfo *cpe, float *spec_l, float *spec_r)
{
    ICSInfo *ics = &cpe->ics[0];

    for (int g = 0; g < ics->num_window_groups; g++) {
        for (int sfb = 0; sfb < ics->max_sfb; sfb++) {
            bool ms_flag = false;
            if (cpe->ms_mask_present == 1) {
                ms_flag = (cpe->ms_used[g][sfb] != 0);
            } else if (cpe->ms_mask_present != 0) {
                ms_flag = true;
            }

            if (ms_flag && !ics->pns_used[g][sfb]) {
                int start_k = ics->sfb_offsets[sfb];
                int end_k = ics->sfb_offsets[sfb + 1];

                for (int w = 0; w < ics->window_group_length[g]; w++) {
                    float *l_ptr = spec_l + w * 128 + start_k;
                    float *r_ptr = spec_r + w * 128 + start_k;

                    for (int k = start_k; k < end_k; k++) {
                        float m = *l_ptr;
                        float s = *r_ptr;
                        *l_ptr++ = m + s;
                        *r_ptr++ = m - s;
                    }
                }
            }
        }
    }
}

void apply_is_stereo(CPEInfo *cpe, float *spec_l, float *spec_r)
{
    ICSInfo *ics_r = &cpe->ics[1];

    for (int g = 0; g < ics_r->num_window_groups; g++) {
        for (int i = 0; i < ics_r->num_sections[g]; i++) {
            int cb = ics_r->sect_cb[g][i];
            if (cb == HCB_INTENSITY || cb == HCB_INTENSITY2) {
                int start_sfb = ics_r->sect_start[g][i];
                int end_sfb = ics_r->sect_end[g][i];

                for (int sfb = start_sfb; sfb < end_sfb; sfb++) {
                    int sf = ics_r->scalefactors[g][sfb];
                    float scale = powf(0.5f, 0.25f * sf);
                    if (cb == HCB_INTENSITY2) scale = -scale;

                    int start_k = ics_r->sfb_offsets[sfb];
                    int end_k = ics_r->sfb_offsets[sfb + 1];

                    for (int w = 0; w < ics_r->window_group_length[g]; w++) {
                        float *l_ptr = spec_l + w * 128 + start_k;
                        float *r_ptr = spec_r + w * 128 + start_k;

                        for (int k = start_k; k < end_k; k++) {
                            *r_ptr++ = (*l_ptr++) * scale;
                        }
                    }
                }
            }
        }
    }
}
