/****************************************************************************
    FAAC Stereo Refactor - Automatic Joint Stereo Decision System

    Copyright (C) 2017 Krzysztof Nikiel
    Refactored by Jules

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

static float calc_cost(CoderInfo *coder, faac_real *spec, faac_real thr, int start, int end, int gsize, float lambda, int mode)
{
    int i, win;
    faac_real distortion = 0;
    int bits = 0;
    int n = end - start;

    for (win = 0; win < gsize; win++) {
        faac_real *s = spec + win * BLOCK_LEN_SHORT + start;
        for (i = 0; i < n; i++) {
            distortion += s[i] * s[i];
        }
    }

    // Better bit estimation:
    // LR/MS both channels: n * bits_per_line * 2
    // IS only one channel: n * bits_per_line + side_info
    // We use a simple heuristic where one channel costs roughly 3 bits per line.
    if (mode == MODE_IS) {
        bits = n * 3 + 10; // 10 bits for side info
    } else {
        bits = n * 3 * 2;
    }

    return (float)(distortion / (thr + 1e-20f)) + lambda * bits;
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

    // Initialization: Clear books and scalefactors for all active channels
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
        // Essential guard: only process channel pairs once, and only for CPE
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

                faac_real corr2 = (cross * cross) / (EL * ER + 1e-20);
                faac_real corr = (cross > 0 ? 1 : -1) * FAAC_SQRT(corr2);
                faac_real EM = (EL + ER + 2 * cross) / 4;
                faac_real ES = (EL + ER - 2 * cross) / 4;
                faac_real side_ratio = ES / (EM + 1e-20);
                faac_real energy_diff = FAAC_FABS(EL - ER) / (EL + ER + 1e-20);
                faac_real band_center_freq = (faac_real)(start + end) * hEncoder->sampleRate / (2.0 * FRAME_LEN);

                int band_mode = MODE_AMBIGUOUS;

                if (corr > 0.85 && side_ratio < 0.15)
                    band_mode = MODE_MS;
                else if (corr < 0.20 || side_ratio > 0.50)
                    band_mode = MODE_LR;
                else if (band_center_freq > 6000 && energy_diff < 0.20 && FAAC_FABS(corr) < 0.30 && intensity_stereo_enabled)
                    band_mode = MODE_IS;

                float final_costLR = 1e30f, final_costMS = 1e30f, final_costIS = 1e30f;

                if (band_mode == MODE_AMBIGUOUS) {
                    final_costLR = calc_cost(coder + chn, s[chn] + start_win * BLOCK_LEN_SHORT, bandlvlL[sfb], start, end, gsize, lambda, MODE_LR) +
                                   calc_cost(coder + rch, s[rch] + start_win * BLOCK_LEN_SHORT, bandlvlr[sfb], start, end, gsize, lambda, MODE_LR);

                    faac_real thrMS = (bandlvlL[sfb] + bandlvlr[sfb]) * 0.5;

                    faac_real M[FRAME_LEN], S[FRAME_LEN];
                    memset(M, 0, sizeof(M));
                    memset(S, 0, sizeof(S));
                    for (win = 0; win < gsize; win++) {
                        faac_real *sl = s[chn] + (start_win + win) * BLOCK_LEN_SHORT;
                        faac_real *sr = s[rch] + (start_win + win) * BLOCK_LEN_SHORT;
                        for (l = start; l < end; l++) {
                            M[win * BLOCK_LEN_SHORT + l] = (sl[l] + sr[l]) * 0.5;
                            S[win * BLOCK_LEN_SHORT + l] = (sl[l] - sr[l]) * 0.5;
                        }
                    }

                    final_costMS = (calc_cost(coder + chn, M, thrMS, start, end, gsize, lambda, MODE_MS) +
                                    calc_cost(coder + rch, S, thrMS, start, end, gsize, lambda, MODE_MS)) * 0.98f;

                    if (band_center_freq > 6000 && energy_diff < 0.20 && intensity_stereo_enabled) {
                        final_costIS = calc_cost(coder + chn, M, thrMS, start, end, gsize, lambda, MODE_IS);
                        if (final_costIS < final_costLR && final_costIS < final_costMS) {
                            band_mode = MODE_IS;
                        } else {
                            band_mode = (final_costMS < final_costLR) ? MODE_MS : MODE_LR;
                        }
                    } else {
                        band_mode = (final_costMS < final_costLR) ? MODE_MS : MODE_LR;
                    }
                }

                // Temporal Hysteresis
                int old_mode = hEncoder->last_ms_used[chn][sfcnt]; // Mapping: 0=LR, 1=MS, 2=IS
                if (band_mode != old_mode && (final_costLR < 1e20f)) {
                    float current_cost = (band_mode == MODE_LR) ? final_costLR : (band_mode == MODE_MS ? final_costMS : final_costIS);
                    float old_cost = (old_mode == MODE_LR) ? final_costLR : (old_mode == MODE_MS ? final_costMS : final_costIS);

                    if (current_cost > (old_cost * 0.95f)) {
                        band_mode = old_mode;
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
                    int sf = FAAC_LRINT(FAAC_LOG10(EL / (efix + 1e-20)) * step);
                    int pan = FAAC_LRINT(FAAC_LOG10(ER / (efix + 1e-20)) * step) - sf;

                    if (pan > 30) {
                        coder[chn].book[sfcnt] = HCB_ZERO;
                    } else if (pan < -30) {
                        coder[rch].book[sfcnt] = HCB_ZERO;
                    } else {
                        coder[chn].sf[sfcnt] = sf;
                        coder[rch].sf[sfcnt] = -pan;
                        coder[rch].book[sfcnt] = hcb;

                        for (win = start_win; win < end_win; win++) {
                            faac_real *sl = s[chn] + win * BLOCK_LEN_SHORT;
                            faac_real *sr = s[rch] + win * BLOCK_LEN_SHORT;
                            for (l = start; l < end; l++) {
                                faac_real val = (hcb == HCB_INTENSITY) ? (sl[l] + sr[l]) : (sl[l] - sr[l]);
                                sl[l] = val * vfix;
                                sr[l] = 0;
                            }
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
