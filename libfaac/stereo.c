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
    along with this program.  See <http://www.gnu.org/licenses/>.
****************************************************************************/

#define _USE_MATH_DEFINES

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "stereo.h"
#include "huff2.h"
#include "util.h"

static void apply_stereo_band(CoderInfo *cl, CoderInfo *cr, ChannelInfo *chi,
                              faac_real *sl0, faac_real *sr0, int *sfcnt,
                              int sfb, int wstart, int wend,
                              faac_real thrmid, faac_real thrside,
                              faac_real phthr, int force_is_sfb,
                              int mode, unsigned long sampleRate,
                              faac_real quality)
{
    int win, l;
    int start = cl->sfb_offset[sfb];
    int end = cl->sfb_offset[sfb + 1];
    faac_real enrgl, enrgr, enrgs, enrgd;
    faac_real sum, diff;

    enrgl = enrgr = enrgs = enrgd = 0.0;

    for (win = wstart; win < wend; win++)
    {
        faac_real *sl = sl0 + win * BLOCK_LEN_SHORT;
        faac_real *sr = sr0 + win * BLOCK_LEN_SHORT;

        for (l = start; l < end; l++)
        {
            faac_real lx = sl[l];
            faac_real rx = sr[l];

            enrgl += lx * lx;
            enrgr += rx * rx;

            sum = lx + rx;
            diff = lx - rx;
            enrgs += sum * sum;
            enrgd += diff * diff;
        }
    }

    /* Decision logic: Intensity Stereo (IS) > Mid/Side (M/S) > L/R */
    int use_is = (force_is_sfb >= 0 && sfb >= force_is_sfb);
    if (!use_is && phthr > 0)
    {
        faac_real ethr = FAAC_SQRT(enrgl) + FAAC_SQRT(enrgr);
        ethr *= ethr * (1.0 / phthr);
        if (enrgs >= ethr || enrgd >= ethr)
            use_is = 1;
    }

    if (use_is && (enrgl + enrgr) < 1e-9) use_is = 0;

    if (use_is)
    {
        int hcb = HCB_INTENSITY;
        faac_real vfix;
        const faac_real step = 10/1.50515;
        faac_real efix = max(enrgl + enrgr, 1e-10);

        if (enrgd > enrgs && enrgd > 1e-10) {
            hcb = HCB_INTENSITY2;
            vfix = FAAC_SQRT(efix / enrgd);
        } else {
            hcb = HCB_INTENSITY;
            vfix = FAAC_SQRT(efix / max(enrgs, 1e-10));
        }

        int sf = (int)FAAC_LRINT(FAAC_LOG10(max(enrgl, 1e-10) / efix) * step);
        int pan = (int)FAAC_LRINT(FAAC_LOG10(max(enrgr, 1e-10) / efix) * step) - sf;

        if (pan > 30) {
            cl->book[*sfcnt] = HCB_ZERO;
        } else if (pan < -30) {
            cr->book[*sfcnt] = HCB_ZERO;
        } else {
            cr->book[*sfcnt] = hcb;
            cl->sf[*sfcnt] = sf;
            cr->sf[*sfcnt] = -pan;

            for (win = wstart; win < wend; win++)
            {
                faac_real *sl = sl0 + win * BLOCK_LEN_SHORT;
                faac_real *sr = sr0 + win * BLOCK_LEN_SHORT;
                for (l = start; l < end; l++)
                {
                    sum = (hcb == HCB_INTENSITY) ? (sl[l] + sr[l]) : (sl[l] - sr[l]);
                    sl[l] = sum * vfix;
                    sr[l] = 0.0;
                }
            }
        }
    }
    else if (mode == JOINT_MS)
    {
        if ((min(enrgl, enrgr) * thrmid) >= max(enrgs, enrgd))
        {
            chi->msInfo.ms_used[*sfcnt] = 1;
            for (win = wstart; win < wend; win++)
            {
                faac_real *sl = sl0 + win * BLOCK_LEN_SHORT;
                faac_real *sr = sr0 + win * BLOCK_LEN_SHORT;
                for (l = start; l < end; l++)
                {
                    /* Legacy Destructive M/S Transform */
                    faac_real s_sum, s_diff;
                    if (enrgs >= enrgd) {
                        s_sum = sl[l] + sr[l];
                        s_diff = 0;
                    } else {
                        s_sum = 0;
                        s_diff = sl[l] - sr[l];
                    }
                    sl[l] = 0.5 * s_sum;
                    sr[l] = 0.5 * s_diff;
                }
            }
        }
    }
}

void AACstereo(CoderInfo *coder,
               ChannelInfo *channel,
               faac_real *s[MAX_CHANNELS],
               int maxchan,
               faac_real quality,
               int mode,
               unsigned long sampleRate
              )
{
    int chn;
    static const faac_real thr075 = 1.09 - 1.0;
    static const faac_real thrmax = 1.25 - 1.0;
    static const faac_real sidemin = 0.1;
    static const faac_real sidemax = 0.3;
    static const faac_real isthrmax = M_SQRT2 - 1.0;
    faac_real thrmid, thrside, isthr;

    thrmid = thr075 / quality;
    if (thrmid > thrmax) thrmid = thrmax;
    thrside = sidemin / quality;
    if (thrside > sidemax) thrside = sidemax;
    thrmid = (thrmid + 1.0) * (thrmid + 1.0);
    thrside = (thrside + 1.0) * (thrside + 1.0);

    isthr = 0.18 / (quality * quality);
    if (isthr > isthrmax) isthr = isthrmax;
    isthr = (isthr + 1.0) * (isthr + 1.0);

    for (chn = 0; chn < maxchan; chn++)
    {
        int group, band;
        int bookcnt = 0;
        CoderInfo *cp = coder + chn;

        if (!channel[chn].present)
            continue;

        for (group = 0; group < cp->groups.n; group++)
        {
            for (band = 0; band < cp->sfbn; band++)
            {
                cp->book[bookcnt] = HCB_NONE;
                cp->sf[bookcnt] = 0;
                bookcnt++;
            }
        }
        if (channel[chn].cpe && channel[chn].ch_is_left)
        {
            for (band = 0; band < MAX_SCFAC_BANDS; band++)
                channel[chn].msInfo.ms_used[band] = 0;
        }
    }

    for (chn = 0; chn < maxchan; chn++)
    {
        int rch, group, band, sfcnt, start;
        CoderInfo *cl = coder + chn;

        if (!channel[chn].present || !channel[chn].ch_is_left || !channel[chn].cpe)
            continue;

        rch = channel[chn].paired_ch;
        CoderInfo *cr = coder + rch;

        if (cl->block_type != cr->block_type || cl->groups.n != cr->groups.n)
            continue;

        int common = 1;
        for (band = 0; band < cl->groups.n; band++)
            if (cl->groups.len[band] != cr->groups.len[band]) common = 0;
        if (!common) continue;

        channel[chn].common_window = 1;
        channel[chn].msInfo.is_present = (mode == JOINT_MS);
        channel[rch].msInfo.is_present = (mode == JOINT_MS);

        int force_is_sfb = -1;
        if (quality < 0.7)
        {
            /* Iteration 40: Optimal thresholds found so far */
            float freq_thresh = 18000.0;
            if (quality < 0.3) freq_thresh = 5000.0;

            int frame_len = (cl->block_type == ONLY_SHORT_WINDOW) ? 128 : 1024;
            for (band = 0; band < cl->sfbn; band++) {
                if (((float)cl->sfb_offset[band] * (float)sampleRate / (float)(frame_len * 2)) >= freq_thresh) {
                    force_is_sfb = band;
                    break;
                }
            }
        }

        sfcnt = 0;
        start = 0;
        for (group = 0; group < cl->groups.n; group++)
        {
            int end = start + cl->groups.len[group];
            int sfmin = (cl->block_type == ONLY_SHORT_WINDOW) ? 1 : 8;

            for (band = 0; band < sfmin; band++)
            {
                channel[chn].msInfo.ms_used[sfcnt++] = 0;
            }

            for (band = sfmin; band < cl->sfbn; band++)
            {
                apply_stereo_band(cl, cr, channel + chn, s[chn], s[rch], &sfcnt,
                                  band, start, end, thrmid, thrside, (mode == JOINT_IS) ? isthr : 0.0,
                                  force_is_sfb, mode, sampleRate, quality);
                sfcnt++;
            }
            start = end;
        }
    }
}
