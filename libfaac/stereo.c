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
    faac_real sum;

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

            /* Unscaled energy for tool selection. */
            faac_real s_is = lx + rx;
            faac_real d_is = lx - rx;
            enrgs += s_is * s_is;
            enrgd += d_is * d_is;
        }
    }

    /* Decision logic: Intensity Stereo (IS) > Mid/Side (M/S) > L/R */
    int use_is = (force_is_sfb >= 0 && sfb >= force_is_sfb);
    if (!use_is && phthr > 1.0)
    {
        faac_real ethr = FAAC_SQRT(enrgl) + FAAC_SQRT(enrgr);
        ethr *= ethr * phthr;
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

        if (enrgd > enrgs) {
            hcb = HCB_INTENSITY2;
            vfix = FAAC_SQRT(efix / max(enrgd, 1e-10));
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
        /* M/S Decision with Phase Dominance Gate 2 restored. */
        faac_real ms_aggression = (quality < 0.35) ? 2.0 : 1.0;
        int ms = 0;
        enum {PH_NONE, PH_IN, PH_OUT};
        int phase = PH_NONE;

        if ((min(enrgl, enrgr) * thrmid * ms_aggression) >= (max(enrgs, enrgd) * 0.25))
        {
            if ((enrgs * 0.25 * thrmid * 2.0 * ms_aggression >= (enrgl + enrgr))) {
                ms = 1; phase = PH_IN;
            } else if ((enrgd * 0.25 * thrmid * 2.0 * ms_aggression >= (enrgl + enrgr))) {
                ms = 1; phase = PH_OUT;
            }
        }

        if (ms)
        {
            chi->msInfo.ms_used[*sfcnt] = 1;
            for (win = wstart; win < wend; win++)
            {
                faac_real *sl = sl0 + win * BLOCK_LEN_SHORT;
                faac_real *sr = sr0 + win * BLOCK_LEN_SHORT;
                for (l = start; l < end; l++)
                {
                    faac_real s_sum, s_diff;
                    if (phase == PH_IN) {
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

        /* Side-channel masking (restricted to non-MS bands). */
        if (!ms && quality < 0.6 && (min(enrgl, enrgr) <= (thrside * max(enrgl, enrgr))))
        {
            for (win = wstart; win < wend; win++)
            {
                faac_real *sl = sl0 + win * BLOCK_LEN_SHORT;
                faac_real *sr = sr0 + win * BLOCK_LEN_SHORT;
                for (l = start; l < end; l++)
                {
                    if (enrgl < enrgr) sl[l] = 0.0;
                    else sr[l] = 0.0;
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
        int group;
        int sfcnt = 0;

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
        for (int cnt = 0; cnt < coder[chn].groups.n; cnt++)
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

        /* Iteration 132 thresholds (Best Performer). */
        int force_is_sfb = -1;
        CoderInfo *cl = &coder[chn];
        if (mode == JOINT_MS && quality < 0.7) {
            float freq_thresh;
            if      (quality < 0.25) freq_thresh = 3000.0f;
            else if (quality < 0.35) freq_thresh = 5000.0f;
            else if (quality < 0.40) freq_thresh = 10000.0f;
            else                     freq_thresh = 15000.0f;

            int frame_len = (cl->block_type == ONLY_SHORT_WINDOW) ? 128 : 1024;
            for (int band = 0; band < cl->sfbn; band++) {
                float f = (float)cl->sfb_offset[band] * sampleRate / (float)(frame_len * 2);
                if (f >= freq_thresh) {
                    force_is_sfb = band;
                    break;
                }
            }
        }

        sfcnt = 0;
        int start = 0;
        for (group = 0; group < cl->groups.n; group++)
        {
            int end = start + cl->groups.len[group];
            int sfmin = (cl->block_type == ONLY_SHORT_WINDOW) ? 1 : 8;

            for (int band = 0; band < sfmin; band++)
            {
                channel[chn].msInfo.ms_used[sfcnt++] = 0;
            }

            for (int band = sfmin; band < cl->sfbn; band++)
            {
                apply_stereo_band(cl, &coder[rch], channel + chn, s[chn], s[rch], &sfcnt,
                                  band, start, end, thrmid, thrside, (mode == JOINT_IS) ? isthr : 0.0,
                                  force_is_sfb, mode, sampleRate, quality);
                sfcnt++;
            }
            start = end;
        }
        skip:;
    }
}
