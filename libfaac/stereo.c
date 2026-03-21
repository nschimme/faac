/****************************************************************************
    Intensity Stereo

    Copyright (C) 2017 Krzysztof Nikiel

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
****************************************************************************/

#define _USE_MATH_DEFINES

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "stereo.h"
#include "huff2.h"

static void ApplyPseudoSBR(faacEncHandle hpEncoder, CoderInfo *coder, faac_real *freq, int ch, int g, int w_offset)
{
    faacEncStruct* hEncoder = (faacEncStruct*)hpEncoder;
    int b, bin, w;
    int block_len = (coder->block_type == ONLY_SHORT_WINDOW) ? BLOCK_LEN_SHORT : BLOCK_LEN_LONG;

    /* Strategy 3: Protection Floor */
    if (coder->frame_pe > 5.0f) return;

    /* 32kbps check - but we rely on hEncoder->sbr_enabled if set in frame.c */
    if (!hEncoder->sbr_enabled) return;

    /* Hybrid Crossover: Find the first zeroed SFB above 10kHz (or 80% of bandwidth) */
    faac_real crossover_floor = 10000.0f;
    int max_avail_bin = coder->sfb_offset[coder->sfbn];
    faac_real max_avail_freq = (faac_real)max_avail_bin * hEncoder->sampleRate / (block_len * 2);
    if (crossover_floor > max_avail_freq * 0.8f) crossover_floor = max_avail_freq * 0.8f;

    int sbr_start_sfb = -1;
    for (b = 0; b < coder->sfbn; b++)
    {
        int start_bin = coder->sfb_offset[b];
        faac_real sfb_freq = (faac_real)start_bin * hEncoder->sampleRate / (block_len * 2);

        if (sbr_start_sfb == -1 && sfb_freq >= crossover_floor)
        {
            int sfcnt = g * coder->sfbn + b;
            if (coder->book[sfcnt] == HCB_ZERO)
            {
                sbr_start_sfb = b;
            }
        }
    }

    /* BAND REVIVAL: Fill entirely zeroed bands above the floor */
    if (sbr_start_sfb != -1)
    {
        for (b = sbr_start_sfb; b < coder->sfbn; b++)
        {
            int sfcnt = g * coder->sfbn + b;
            if (coder->book[sfcnt] != HCB_ZERO) continue;

            int b_bin_start = coder->sfb_offset[b];
            int b_bin_end = coder->sfb_offset[b+1];

            /* Harmonic Search (More precise) */
            int best_offset = 0;
            faac_real max_corr = -1.0f;
            for (int off = -64; off <= 64; off += 2) {
                faac_real corr = 0;
                for (w = 0; w < coder->groups.len[g]; w++) {
                    faac_real *wfreq = freq + (w_offset + w) * block_len;
                    faac_real *orig = hEncoder->origFreqBuff[ch] + (w_offset + w) * block_len;
                    for (int i = b_bin_start; i < b_bin_end; i++) {
                        int src = i / 2 + off;
                        if (src < 0 || src >= block_len) continue;
                        corr += (faac_real)fabs(wfreq[src] * orig[i]);
                    }
                }
                if (corr > max_corr) {
                    max_corr = corr;
                    best_offset = off;
                }
            }

            /* Adaptive Tilt */
            faac_real sfm = 0.5f;
            faac_real sum_val = 0, sum_log = 0;
            int count = 0;
            for (w = 0; w < coder->groups.len[g]; w++) {
                faac_real *wfreq = freq + (w_offset + w) * block_len;
                for (int i = b_bin_start; i < b_bin_end; i++) {
                    int src = i / 2 + best_offset;
                    if (src < 0 || src >= block_len) continue;
                    faac_real val = (faac_real)fabs(wfreq[src]);
                    sum_val += val;
                    sum_log += (faac_real)log(val + 1e-10);
                    count++;
                }
            }
            if (count > 0) {
                faac_real am = sum_val / count;
                faac_real gm = (faac_real)exp(sum_log / count);
                sfm = (am > 1e-10) ? gm / am : 0.5f;
            }

            faac_real sfm_adj_db = (sfm < 0.3f) ? -6.0f : (sfm > 0.7f) ? 3.0f : 0.0f;

            for (w = 0; w < coder->groups.len[g]; w++)
            {
                faac_real *wfreq = freq + (w_offset + w) * block_len;
                for (bin = b_bin_start; bin < b_bin_end; bin++)
                {
                    int source_index = bin / 2 + best_offset;
                    if (source_index < 0) source_index = 0;
                    if (source_index >= block_len) source_index = block_len - 1;

                    faac_real bin_freq = (faac_real)bin * hEncoder->sampleRate / (block_len * 2);
                    faac_real tilt_db = -12.0f * (bin_freq - crossover_floor) / (16000.0f - crossover_floor);
                    if (tilt_db > 0) tilt_db = 0;

                    faac_real total_gain_db = tilt_db + sfm_adj_db - 2.0f;
                    faac_real gain = (faac_real)pow(10.0, total_gain_db / 20.0);

                    if (b == sbr_start_sfb && (bin - b_bin_start) < 3)
                    {
                        gain *= (faac_real)(bin - b_bin_start + 1) / 4.0f;
                    }

                    wfreq[bin] = wfreq[source_index] * gain;
                    wfreq[bin] += ((rand() / (faac_real)RAND_MAX * 2.0f - 1.0f) * 0.005f);
                }
            }
            coder->book[sfcnt] = HCB_ESC;
        }
    }
}

static void stereo(CoderInfo *cl, CoderInfo *cr,
                   faac_real *sl0, faac_real *sr0, int *sfcnt,
                   int wstart, int wend, faac_real phthr
                  )
{
    int sfb;
    int win;
    int sfmin;

    if (!phthr)
        return;

    phthr = 1.0 / phthr;

    if (cl->block_type == ONLY_SHORT_WINDOW)
        sfmin = 1;
    else
        sfmin = 8;

    (*sfcnt) += sfmin;

    for (sfb = sfmin; sfb < cl->sfbn; sfb++)
    {
        int l, start, end;
        faac_real sum, diff;
        faac_real enrgs, enrgd, enrgl, enrgr;
        int hcb = HCB_NONE;
        const faac_real step = 10/1.50515;
        faac_real ethr;
        faac_real vfix, efix;

        start = cl->sfb_offset[sfb];
        end = cl->sfb_offset[sfb + 1];

        enrgs = enrgd = enrgl = enrgr = 0.0;
        for (win = wstart; win < wend; win++)
        {
            faac_real *sl = sl0 + win * BLOCK_LEN_SHORT;
            faac_real *sr = sr0 + win * BLOCK_LEN_SHORT;

            for (l = start; l < end; l++)
            {
                faac_real lx = sl[l];
                faac_real rx = sr[l];

                sum = lx + rx;
                diff = lx - rx;

                enrgs += sum * sum;
                enrgd += diff * diff;
                enrgl += lx * lx;
                enrgr += rx * rx;
            }
        }

        ethr = FAAC_SQRT(enrgl) + FAAC_SQRT(enrgr);
        ethr *= ethr;
        ethr *= phthr;
        efix = enrgl + enrgr;
        if (enrgs >= ethr)
        {
            hcb = HCB_INTENSITY;
            vfix = FAAC_SQRT(efix / enrgs);
        }
        else if (enrgd >= ethr)
        {
            hcb = HCB_INTENSITY2;
            vfix = FAAC_SQRT(efix / enrgd);
        }

        if (hcb != HCB_NONE)
        {
            int sf = FAAC_LRINT(FAAC_LOG10(enrgl / efix) * step);
            int pan = FAAC_LRINT(FAAC_LOG10(enrgr/efix) * step) - sf;

            if (pan > 30)
            {
                cl->book[*sfcnt] = HCB_ZERO;
                (*sfcnt)++;
                continue;
            }
            if (pan < -30)
            {
                cr->book[*sfcnt] = HCB_ZERO;
                (*sfcnt)++;
                continue;
            }
            cl->sf[*sfcnt] = sf;
            cr->sf[*sfcnt] = -pan;
            cr->book[*sfcnt] = hcb;

            for (win = wstart; win < wend; win++)
            {
                faac_real *sl = sl0 + win * BLOCK_LEN_SHORT;
                faac_real *sr = sr0 + win * BLOCK_LEN_SHORT;
                for (l = start; l < end; l++)
                {
                    if (hcb == HCB_INTENSITY)
                        sum = sl[l] + sr[l];
                    else
                        sum = sl[l] - sr[l];

                    sl[l] = sum * vfix;
                }
            }
        }
        (*sfcnt)++;
    }
}

static void midside(CoderInfo *coder, ChannelInfo *channel,
                    faac_real *sl0, faac_real *sr0, int *sfcnt,
                    int wstart, int wend,
                    faac_real thrmid, faac_real thrside
                   )
{
    int sfb;
    int win;
    int sfmin;

    if (coder->block_type == ONLY_SHORT_WINDOW)
        sfmin = 1;
    else
        sfmin = 8;

    for (sfb = 0; sfb < sfmin; sfb++)
    {
        channel->msInfo.ms_used[*sfcnt] = 0;
        (*sfcnt)++;
    }
    for (sfb = sfmin; sfb < coder->sfbn; sfb++)
    {
        int ms = 0;
        int l, start, end;
        faac_real sum, diff;
        faac_real enrgs, enrgd, enrgl, enrgr;

        start = coder->sfb_offset[sfb];
        end = coder->sfb_offset[sfb + 1];

        enrgs = enrgd = enrgl = enrgr = 0.0;
        for (win = wstart; win < wend; win++)
        {
            faac_real *sl = sl0 + win * BLOCK_LEN_SHORT;
            faac_real *sr = sr0 + win * BLOCK_LEN_SHORT;

            for (l = start; l < end; l++)
            {
                faac_real lx = sl[l];
                faac_real rx = sr[l];

                sum = 0.5 * (lx + rx);
                diff = 0.5 * (lx - rx);

                enrgs += sum * sum;
                enrgd += diff * diff;
                enrgl += lx * lx;
                enrgr += rx * rx;
            }
        }

        if ((min(enrgl, enrgr) * thrmid) >= max(enrgs, enrgd))
        {
            enum {PH_NONE, PH_IN, PH_OUT};
            int phase = PH_NONE;

            if ((enrgs * thrmid * 2.0) >= (enrgl + enrgr))
            {
                ms = 1;
                phase = PH_IN;
            }
            else if ((enrgd * thrmid * 2.0) >= (enrgl + enrgr))
            {
                ms = 1;
                phase = PH_OUT;
            }

            if (ms)
            {
                for (win = wstart; win < wend; win++)
                {
                    faac_real *sl = sl0 + win * BLOCK_LEN_SHORT;
                    faac_real *sr = sr0 + win * BLOCK_LEN_SHORT;
                    for (l = start; l < end; l++)
                    {
                        if (phase == PH_IN)
                        {
                            sum = sl[l] + sr[l];
                            diff = 0;
                        }
                        else
                        {
                            sum = 0;
                            diff = sl[l] - sr[l];
                        }

                        sl[l] = 0.5 * sum;
                        sr[l] = 0.5 * diff;
                    }
                }
            }
        }

        if (min(enrgl, enrgr) <= (thrside * max(enrgl, enrgr)))
        {
            for (win = wstart; win < wend; win++)
            {
                faac_real *sl = sl0 + win * BLOCK_LEN_SHORT;
                faac_real *sr = sr0 + win * BLOCK_LEN_SHORT;
                for (l = start; l < end; l++)
                {
                    if (enrgl < enrgr)
                        sl[l] = 0.0;
                    else
                        sr[l] = 0.0;
                }
            }
        }

        channel->msInfo.ms_used[*sfcnt] = ms;
        (*sfcnt)++;
    }
}


void AACstereo(faacEncHandle hpEncoder,
               CoderInfo *coder,
               ChannelInfo *channel,
               faac_real *s[MAX_CHANNELS],
               int maxchan,
               faac_real quality,
               int mode
              )
{
    int chn;
    static const faac_real thr075 = 1.09 /* ~0.75dB */ - 1.0;
    static const faac_real thrmax = 1.25 /* ~2dB */ - 1.0;
    static const faac_real sidemin = 0.1; /* -20dB */
    static const faac_real sidemax = 0.3; /* ~-10.5dB */
    static const faac_real isthrmax = M_SQRT2 - 1.0;
    faac_real thrmid, thrside;
    faac_real isthr;

    thrmid = 1.0;
    thrside = 0.0;
    isthr = 1.0;

    switch (mode)
    {
    case JOINT_MS:
        thrmid = thr075 / quality;
        if (thrmid > thrmax)
            thrmid = thrmax;

        thrside = sidemin / quality;
        if (thrside > sidemax)
            thrside = sidemax;

        thrmid += 1.0;
        break;
    case JOINT_IS:
        isthr = 0.18 / (quality * quality);
        if (isthr > isthrmax)
            isthr = isthrmax;

        isthr += 1.0;
        break;
    }

    // convert into energy
    thrmid *= thrmid;
    thrside *= thrside;
    isthr *= isthr;

    for (chn = 0; chn < maxchan; chn++)
    {
        int group;
        int bookcnt = 0;
        CoderInfo *cp = coder + chn;

        if (!channel[chn].present)
            continue;

        for (group = 0; group < cp->groups.n; group++)
        {
            int band;
            for (band = 0; band < cp->sfbn; band++)
            {
                cp->book[bookcnt] = HCB_NONE;
                cp->sf[bookcnt] = 0;
                bookcnt++;
            }
        }
    }
    for (chn = 0; chn < maxchan; chn++)
    {
        int rch;
        int cnt;
        int group;
        int sfcnt = 0;
        int start = 0;

        if (!channel[chn].present)
            continue;
        if (!((channel[chn].cpe) && (channel[chn].ch_is_left)))
            continue;

        rch = channel[chn].paired_ch;

        channel[chn].common_window = 0;
        channel[chn].msInfo.is_present = 0;
        channel[rch].msInfo.is_present = 0;

        if (coder[chn].block_type != coder[rch].block_type)
            continue;
        if (coder[chn].groups.n != coder[rch].groups.n)
            continue;

        channel[chn].common_window = 1;
        for (cnt = 0; cnt < coder[chn].groups.n; cnt++)
            if (coder[chn].groups.len[cnt] != coder[rch].groups.len[cnt])
            {
                channel[chn].common_window = 0;
                goto skip;
            }

        if (mode == JOINT_MS)
        {
            channel[chn].common_window = 1;
            channel[chn].msInfo.is_present = 1;
            channel[rch].msInfo.is_present = 1;
        }

        for (group = 0; group < coder->groups.n; group++)
        {
            int end = start + coder->groups.len[group];
            switch(mode) {
            case JOINT_MS:
                midside(coder + chn, channel + chn, s[chn], s[rch], &sfcnt,
                        start, end, thrmid, thrside);
                /* Apply Pseudo-SBR to the Mid (Left) channel after M/S */
                ApplyPseudoSBR(hpEncoder, coder + chn, s[chn], chn, group, start);
                break;
            case JOINT_IS:
                stereo(coder + chn, coder + rch, s[chn], s[rch], &sfcnt, start, end, isthr);
                /* Apply Pseudo-SBR to the Mid (Left) channel after Intensity Stereo */
                ApplyPseudoSBR(hpEncoder, coder + chn, s[chn], chn, group, start);
                break;
            }
            start = end;
        }
        skip:;
    }

    /* Support for Mono and Non-Joint Stereo channels */
    for (chn = 0; chn < maxchan; chn++)
    {
        if (!channel[chn].present) continue;

        /* Skip channels already handled in the joint loop (Left channel of CPE)
           or Right channel of IS/MS pair which doesn't need independent SBR here. */
        if (channel[chn].cpe) continue;

        int win_offset = 0;
        for (int g = 0; g < coder[chn].groups.n; g++)
        {
            ApplyPseudoSBR(hpEncoder, &coder[chn], s[chn], chn, g, win_offset);
            win_offset += coder[chn].groups.len[g];
        }
    }
}
