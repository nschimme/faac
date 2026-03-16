/****************************************************************************
    FAAC Stereo Refactor - Automatic Joint Stereo Decision System

    Copyright (C) 2017 Krzysztof Nikiel
    Refactored and Optimized by Jules

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2.1 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
****************************************************************************/

#include <math.h>
#include <string.h>
#include "stereo.h"
#include "huff2.h"
#include "frame.h"
#include "quantize.h"

enum StereoMode
{
    MODE_LR = 0,
    MODE_MS = 1,
    MODE_IS = 2,
    MODE_AMBIGUOUS = 3
};

static float calc_cost(faac_real *spec, faac_real thr, int start, int end, int gsize, float lambda, int mode)
{
    int i, win;
    faac_real distortion = 0;
    faac_real pe = 0;
    int n = end - start;
    const faac_real inv_log2 = 1.4426950408889634;

    for (win = 0; win < gsize; win++) {
        faac_real *s = spec + win * BLOCK_LEN_SHORT + start;
        for (i = 0; i < n; i++) {
            faac_real val2 = s[i] * s[i];
            distortion += val2;
            // Perceptual Entropy heuristic: bits proportional to log2(1 + SMR)
            if (val2 > thr) {
                pe += log(1.0 + val2 / (thr + 1e-20)) * inv_log2;
            }
        }
    }

    if (mode == MODE_IS) {
        pe = pe * 0.5 + 10; // Only one channel remains
    }

    return (float)(distortion / (thr + 1e-20f)) + lambda * pe;
}

static inline int evaluate_ambiguous(CoderInfo *coder, int chn, int rch, int sfb, int start, int end, int start_win, int gsize,
                                     faac_real *s[MAX_CHANNELS], faac_real bandlvlL, faac_real bandlvlr,
                                     float lambda, int intensity_stereo_enabled, faac_real band_center_freq, faac_real energy_diff,
                                     faac_real *M_buf, faac_real *S_buf,
                                     float *out_LR, float *out_MS, float *out_IS)
{
    int l, win;
    *out_LR = calc_cost(s[chn] + start_win * BLOCK_LEN_SHORT, bandlvlL, start, end, gsize, lambda, MODE_LR) +
              calc_cost(s[rch] + start_win * BLOCK_LEN_SHORT, bandlvlr, start, end, gsize, lambda, MODE_LR);

    faac_real thrMS = (bandlvlL + bandlvlr) * 0.5;

    for (win = 0; win < gsize; win++) {
        faac_real *sl = s[chn] + (start_win + win) * BLOCK_LEN_SHORT;
        faac_real *sr = s[rch] + (start_win + win) * BLOCK_LEN_SHORT;
        for (l = start; l < end; l++) {
            M_buf[win * BLOCK_LEN_SHORT + l] = (sl[l] + sr[l]) * 0.5;
            S_buf[win * BLOCK_LEN_SHORT + l] = (sl[l] - sr[l]) * 0.5;
        }
    }

    *out_MS = (calc_cost(M_buf + start_win * BLOCK_LEN_SHORT, thrMS, start, end, gsize, lambda, MODE_MS) +
               calc_cost(S_buf + start_win * BLOCK_LEN_SHORT, thrMS, start, end, gsize, lambda, MODE_MS)) * 0.98f;

    *out_IS = 1e30f;
    if (band_center_freq > 6000 && intensity_stereo_enabled) {
        *out_IS = calc_cost(M_buf + start_win * BLOCK_LEN_SHORT, thrMS, start, end, gsize, lambda, MODE_IS);
        if (*out_IS < *out_LR && *out_IS < *out_MS) {
            return MODE_IS;
        }
    }

    return (*out_MS < *out_LR) ? MODE_MS : MODE_LR;
}

void AACstereo(faacEncHandle hpEncoder,
               CoderInfo *coder,
               ChannelInfo *channel,
               faac_real *s[MAX_CHANNELS],
               int maxchan
              )
{
    faacEncStruct* hEncoder = (faacEncStruct*)hpEncoder;
    int chn;
    faac_real M_frame[FRAME_LEN], S_frame[FRAME_LEN];

    for (chn = 0; chn < maxchan; chn++) {
        if (!channel[chn].present)
            continue;
        CoderInfo *cp = coder + chn;
        int bookcnt = 0;
        for (int group = 0; group < cp->groups.n; group++) {
            for (int band = 0; band < cp->sfbn; band++) {
                cp->book[bookcnt] = HCB_NONE;
                cp->sf[bookcnt] = 0;
                bookcnt++;
            }
        }
    }

    for (chn = 0; chn < maxchan; chn++) {
        int rch;
        int group;
        int sfcnt = 0;
        int start_win = 0;

        if (!channel[chn].present)
            continue;
        if (!((channel[chn].cpe) && (channel[chn].ch_is_left)))
            continue;

        rch = channel[chn].paired_ch;
        if (rch <= chn) continue;

        if (coder[chn].block_type != coder[rch].block_type) {
            channel[chn].common_window = 0;
            continue;
        }
        if (coder[chn].groups.n != coder[rch].groups.n) {
            channel[chn].common_window = 0;
            continue;
        }

        channel[chn].common_window = 1;
        for (int i = 0; i < coder[chn].groups.n; i++) {
            if (coder[chn].groups.len[i] != coder[rch].groups.len[i]) {
                channel[chn].common_window = 0;
                break;
            }
        }

        if (!channel[chn].common_window)
            continue;

        channel[chn].msInfo.is_present = 1;
        channel[rch].msInfo.is_present = 1;

        float lambda = 0.15f * (hEncoder->config.quantqual / 100.0f);
        int intensity_stereo_enabled = (hEncoder->config.jointmode != JOINT_NONE);

        for (group = 0; group < coder[chn].groups.n; group++) {
            int end_win = start_win + coder[chn].groups.len[group];
            int gsize = coder[chn].groups.len[group];

            faac_real bandlvlL[MAX_SCFAC_BANDS];
            faac_real bandenrgL[MAX_SCFAC_BANDS];
            faac_real bandlvlr[MAX_SCFAC_BANDS];
            faac_real bandenrgR[MAX_SCFAC_BANDS];

            bmask(coder + chn, s[chn] + start_win * BLOCK_LEN_SHORT, bandlvlL, bandenrgL, group, (faac_real)hEncoder->aacquantCfg.quality/DEFQUAL);
            bmask(coder + rch, s[rch] + start_win * BLOCK_LEN_SHORT, bandlvlr, bandenrgR, group, (faac_real)hEncoder->aacquantCfg.quality/DEFQUAL);

            for (int sfb = 0; sfb < coder[chn].sfbn; sfb++) {
                int l, win;
                int start = coder[chn].sfb_offset[sfb];
                int end = coder[chn].sfb_offset[sfb + 1];
                faac_real EL = 0, ER = 0, cross = 0;

                for (win = start_win; win < end_win; win++) {
                    faac_real *sl = s[chn] + win * BLOCK_LEN_SHORT;
                    faac_real *sr = s[rch] + win * BLOCK_LEN_SHORT;
                    for (l = start; l < end; l++) {
                        EL += sl[l] * sl[l];
                        ER += sr[l] * sr[l];
                        cross += sl[l] * sr[l];
                    }
                }

                faac_real denom = EL * ER + 1e-20;
                faac_real corr2 = (cross * cross) / denom;
                faac_real EM = (EL + ER + 2 * cross) / 4;
                faac_real ES = (EL + ER - 2 * cross) / 4;
                faac_real side_ratio = ES / (EM + 1e-20);
                faac_real energy_diff_ratio = FAAC_FABS(EL - ER) / (EL + ER + 1e-20);
                faac_real band_center_freq = (faac_real)(start + end) * hEncoder->sampleRate / (2.0 * FRAME_LEN);

                int band_mode = MODE_AMBIGUOUS;

                // Optimization: Avoid SQRT by using squared thresholds
                // 0.85^2 = 0.7225
                // 0.20^2 = 0.04
                // 0.30^2 = 0.09
                if (cross > 0 && corr2 > 0.7225 && side_ratio < 0.15)
                    band_mode = MODE_MS;
                else if (corr2 < 0.04 || side_ratio > 0.50)
                    band_mode = MODE_LR;
                else if (band_center_freq > 6000 && energy_diff_ratio < 0.20 && corr2 < 0.09 && intensity_stereo_enabled)
                    band_mode = MODE_IS;

                float final_costLR = 0, final_costMS = 0, final_costIS = 1e30f;
                int old_mode = hEncoder->last_ms_used[chn][sfcnt];

                if (band_mode == MODE_AMBIGUOUS || band_mode != old_mode) {
                    int decided = evaluate_ambiguous(coder, chn, rch, sfb, start, end, start_win, gsize, s, bandlvlL[sfb], bandlvlr[sfb],
                                                     lambda, intensity_stereo_enabled, band_center_freq, energy_diff_ratio,
                                                     M_frame, S_frame, &final_costLR, &final_costMS, &final_costIS);

                    if (band_mode == MODE_AMBIGUOUS)
                        band_mode = decided;

                    if (band_mode != old_mode) {
                        float current_cost = (band_mode == MODE_LR) ? final_costLR : (band_mode == MODE_MS ? final_costMS : final_costIS);
                        float old_cost = (old_mode == MODE_LR) ? final_costLR : (old_mode == MODE_MS ? final_costMS : final_costIS);
                        if (current_cost > (old_cost * 0.95f)) {
                            band_mode = old_mode;
                        }
                    }
                }

                // Apply mode
                if (band_mode == MODE_MS) {
                    channel[chn].msInfo.ms_used[sfcnt] = 1;
                    for (win = start_win; win < end_win; win++) {
                        faac_real *sl = s[chn] + win * BLOCK_LEN_SHORT;
                        faac_real *sr = s[rch] + win * BLOCK_LEN_SHORT;
                        for (l = start; l < end; l++) {
                            faac_real L = sl[l];
                            faac_real R = sr[l];
                            sl[l] = (L + R) * 0.5;
                            sr[l] = (L - R) * 0.5;
                        }
                    }
                } else if (band_mode == MODE_IS) {
                    channel[chn].msInfo.ms_used[sfcnt] = 0;

                    faac_real enrgs = EM * 4;
                    faac_real enrgd = ES * 4;
                    int hcb = HCB_INTENSITY;
                    faac_real efix = EL + ER;
                    faac_real vfix;

                    if (enrgd > enrgs) {
                        hcb = HCB_INTENSITY2;
                        vfix = FAAC_SQRT(efix / (enrgd + 1e-20));
                    } else {
                        hcb = HCB_INTENSITY;
                        vfix = FAAC_SQRT(efix / (enrgs + 1e-20));
                    }

                    const faac_real step = 10/1.50515;
                    int pan = FAAC_LRINT(FAAC_LOG10(ER / (EL + 1e-20)) * step);

                    if (pan > 60) pan = 60;
                    if (pan < -60) pan = -60;

                    coder[rch].book[sfcnt] = hcb;
                    coder[rch].sf[sfcnt] = pan;

                    for (win = start_win; win < end_win; win++) {
                        faac_real *sl = s[chn] + win * BLOCK_LEN_SHORT;
                        faac_real *sr = s[rch] + win * BLOCK_LEN_SHORT;
                        for (l = start; l < end; l++) {
                            faac_real val = (hcb == HCB_INTENSITY) ? (sl[l] + sr[l]) : (sl[l] - sr[l]);
                            sl[l] = val * vfix;
                            sr[l] = 0;
                        }
                    }
                } else {
                    channel[chn].msInfo.ms_used[sfcnt] = 0;
                }

                hEncoder->last_ms_used[chn][sfcnt] = band_mode;
                hEncoder->last_ms_used[rch][sfcnt] = band_mode;
                sfcnt++;
            }
            start_win = end_win;
        }
    }
}
