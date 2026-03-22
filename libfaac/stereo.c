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

#include <math.h>
#include "stereo.h"
#include "huff2.h"


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

        for (group = 0; group < coder[chn].groups.n; group++)
        {
            int end = start + coder[chn].groups.len[group];
            switch(mode) {
            case JOINT_MS:
                midside(coder + chn, channel + chn, s[chn], s[rch], &sfcnt,
                        start, end, thrmid, thrside);
                break;
            case JOINT_IS:
                stereo(coder + chn, coder + rch, s[chn], s[rch], &sfcnt, start, end, isthr);
                break;
            }
            start = end;
        }

        /* Pseudo-SBR Folding: Apply post-stereo but pre-quantization */
        if (((faacEncStruct*)hpEncoder)->sbr_enabled) {
            int block_len = (coder[chn].block_type == ONLY_SHORT_WINDOW) ? BLOCK_LEN_SHORT : BLOCK_LEN_LONG;
            int crossover_bin = 0;
            faac_real sbr_floor = ((faacEncStruct*)hpEncoder)->sbr_nominal_bw * 0.8f;
            if (sbr_floor < 8000.0f) sbr_floor = 8000.0f;

            for (int b = 0; b < coder[chn].sfbn; b++) {
                if ((faac_real)coder[chn].sfb_offset[b] * ((faacEncStruct*)hpEncoder)->sampleRate / (block_len * 2) >= sbr_floor) {
                    crossover_bin = coder[chn].sfb_offset[b];
                    break;
                }
            }

            if (crossover_bin > 0 && coder[chn].frame_pe < 7.0f) {
                int group_idx;
                int group_offset = 0;
                for (group_idx = 0; group_idx < coder[chn].groups.n; group_idx++) {
                    int win_len = coder[chn].groups.len[group_idx];
                    int best_offset = 0;
                    faac_real max_corr = -1.0f;

                    /* Harmonic Search (Once per group) */
                    for (int off = -64; off <= 64; off += 4) {
                        faac_real corr = 0;
                        for (int w = 0; w < win_len; w++) {
                            faac_real *win_s = s[chn] + (group_offset + w) * block_len;
                            faac_real *win_orig = ((faacEncStruct*)hpEncoder)->origFreqBuff[chn] + (group_offset + w) * block_len;
                            for (int i = crossover_bin; i < block_len; i++) {
                                int src = i / 2 + off;
                                if (src >= 0 && src < block_len)
                                    corr += (faac_real)fabs(win_s[src] * win_orig[i]);
                            }
                        }
                        if (corr > max_corr) { max_corr = corr; best_offset = off; }
                    }

                    /* Apply Folding with Cross-fade and Noise Injection */
                    for (int w = 0; w < win_len; w++) {
                        faac_real *win_s = s[chn] + (group_offset + w) * block_len;
                        faac_real *win_sr = s[rch] + (group_offset + w) * block_len;
                        for (int i = crossover_bin; i < block_len; i++) {
                            int src = i / 2 + best_offset;
                            if (src < 0) src = 0;
                            if (src >= block_len) src = block_len - 1;

                            faac_real bin_f = (faac_real)i * ((faacEncStruct*)hpEncoder)->sampleRate / (block_len * 2);
                            faac_real tilt_db = -18.0f * (bin_f - sbr_floor) / (16000.0f - sbr_floor);
                            if (tilt_db > 0) tilt_db = 0;
                            faac_real gain = (faac_real)pow(10.0, (tilt_db - 12.0f) / 20.0);

                            /* Cross-fade (4 bins) */
                            faac_real mix = 1.0f;
                            if (i < crossover_bin + 4) mix = (faac_real)(i - crossover_bin) / 4.0f;

                            /* Source Folding */
                            faac_real folded = win_s[src] * gain;

                            /* Comfort Noise Injection (0.005f amplitude) */
                    /* Note: Use localized pseudo-random noise to avoid srand/rand library side-effects */
                    static unsigned int noise_seed = 0x12345678;
                    noise_seed = noise_seed * 1103515245 + 12345;
                    faac_real noise = (faac_real)((noise_seed / 65536) % 32768) / 32768.0f;
                    folded += (noise * 2.0f - 1.0f) * 0.005f;

                            win_s[i] = win_s[i] * (1.0f - mix) + folded * mix;

                            /* If Intensity Stereo is NOT active, apply same folding to right channel */
                            if (channel[chn].cpe && channel[chn].ch_is_left && mode != JOINT_IS) {
                                win_sr[i] = win_sr[i] * (1.0f - mix) + win_sr[src] * gain * mix;
                            }
                        }
                    }
                    group_offset += win_len;
                }
            }
        }
        skip:;
    }
}
