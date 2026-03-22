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

enum { MS_PH_NONE, MS_PH_IN, MS_PH_OUT };

static void apply_is(CoderInfo *cl, CoderInfo *cr,
                     faac_real *sl0, faac_real *sr0,
                     int sfcnt, int start_win, int end_win, int start_bin, int end_bin,
                     int hcb, int sf, int pan, faac_real enrgs, faac_real enrgd, faac_real efix)
{
    int win, l;
    faac_real vfix;

    if (hcb == HCB_INTENSITY)
        vfix = FAAC_SQRT(efix / enrgs);
    else
        vfix = FAAC_SQRT(efix / enrgd);

    cl->sf[sfcnt] = sf;
    cr->sf[sfcnt] = -pan;
    cr->book[sfcnt] = hcb;

    for (win = start_win; win < end_win; win++)
    {
        faac_real *sl = sl0 + win * BLOCK_LEN_SHORT;
        faac_real *sr = sr0 + win * BLOCK_LEN_SHORT;
        for (l = start_bin; l < end_bin; l++)
        {
            faac_real sum;
            if (hcb == HCB_INTENSITY)
                sum = sl[l] + sr[l];
            else
                sum = sl[l] - sr[l];
            sl[l] = sum * vfix;
        }
    }
}

static void apply_ms(ChannelInfo *channel, faac_real *sl0, faac_real *sr0,
                     int sfcnt, int start_win, int end_win, int start_bin, int end_bin,
                     int phase)
{
    int win, l;

    channel->msInfo.ms_used[sfcnt] = 1;

    for (win = start_win; win < end_win; win++)
    {
        faac_real *sl = sl0 + win * BLOCK_LEN_SHORT;
        faac_real *sr = sr0 + win * BLOCK_LEN_SHORT;
        for (l = start_bin; l < end_bin; l++)
        {
            faac_real sum, diff;
            if (phase == MS_PH_IN)
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

static void apply_lr(ChannelInfo *channel, faac_real *sl0, faac_real *sr0,
                     int sfcnt, int start_win, int end_win, int start_bin, int end_bin,
                     faac_real enrgl, faac_real enrgr, faac_real thrside)
{
    int win, l;

    channel->msInfo.ms_used[sfcnt] = 0;

    if (min(enrgl, enrgr) <= (thrside * max(enrgl, enrgr)))
    {
        for (win = start_win; win < end_win; win++)
        {
            faac_real *sl = sl0 + win * BLOCK_LEN_SHORT;
            faac_real *sr = sr0 + win * BLOCK_LEN_SHORT;
            for (l = start_bin; l < end_bin; l++)
            {
                if (enrgl < enrgr)
                    sl[l] = 0.0;
                else
                    sr[l] = 0.0;
            }
        }
    }
}

void AACstereo(CoderInfo *coder,
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

    if (mode == JOINT_NONE)
        return;

    thrmid = 1.0;
    thrside = 0.0;
    isthr = 1.0;

    switch (mode)
    {
    case JOINT_MS:
    case JOINT_MIXED:
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

    if (mode == JOINT_MIXED)
    {
        faac_real m_isthr = 0.18 / (quality * quality);
        if (m_isthr > isthrmax)
            m_isthr = isthrmax;
        isthr = m_isthr + 1.0;
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
        int start_win = 0;

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

        if (mode == JOINT_MS || mode == JOINT_MIXED)
        {
            channel[chn].common_window = 1;
            channel[chn].msInfo.is_present = 1;
            channel[rch].msInfo.is_present = 1;
        }

        for (group = 0; group < coder[chn].groups.n; group++)
        {
            int end_win = start_win + coder[chn].groups.len[group];
            int sfmin;
            int sfb;

            if (coder[chn].block_type == ONLY_SHORT_WINDOW)
                sfmin = 1;
            else
                sfmin = 8;

            for (sfb = 0; sfb < sfmin; sfb++)
            {
                channel[chn].msInfo.ms_used[sfcnt] = 0;
                sfcnt++;
            }

            for (sfb = sfmin; sfb < coder[chn].sfbn; sfb++)
            {
                int l, start_bin, end_bin;
                int win;
                faac_real enrgl, enrgr, enrgs, enrgd;

                start_bin = coder[chn].sfb_offset[sfb];
                end_bin = coder[chn].sfb_offset[sfb + 1];

                enrgl = enrgr = enrgs = enrgd = 0.0;

                for (win = start_win; win < end_win; win++)
                {
                    faac_real *sl = s[chn] + win * BLOCK_LEN_SHORT;
                    faac_real *sr = s[rch] + win * BLOCK_LEN_SHORT;

                    for (l = start_bin; l < end_bin; l++)
                    {
                        faac_real lx = sl[l];
                        faac_real rx = sr[l];
                        faac_real sum = lx + rx;
                        faac_real diff = lx - rx;

                        enrgl += lx * lx;
                        enrgr += rx * rx;
                        enrgs += sum * sum;
                        enrgd += diff * diff;
                    }
                }

                int use_is = 0;
                int use_ms = 0;
                int hcb = HCB_NONE;
                int sf = 0, pan = 0;
                faac_real efix = enrgl + enrgr;

                // 1. Evaluate Intensity Stereo
                if (mode == JOINT_IS || mode == JOINT_MIXED)
                {
                    faac_real ethr = FAAC_SQRT(enrgl) + FAAC_SQRT(enrgr);
                    ethr *= ethr;
                    ethr *= (1.0 / isthr);

                    if (enrgs >= ethr)
                        hcb = HCB_INTENSITY;
                    else if (enrgd >= ethr)
                        hcb = HCB_INTENSITY2;

                    if (hcb != HCB_NONE)
                    {
                        if (efix > 0)
                        {
                            const faac_real step = 10 / 1.50515;
                            sf = FAAC_LRINT(FAAC_LOG10(enrgl / efix) * step);
                            pan = FAAC_LRINT(FAAC_LOG10(enrgr / efix) * step) - sf;

                            if (pan <= 30 && pan >= -30)
                            {
                                use_is = 1;
                            }
                            else
                            {
                                hcb = HCB_NONE;
                            }
                        }
                        else
                        {
                            hcb = HCB_NONE;
                        }
                    }
                }

                // 2. Evaluate M/S
                if (!use_is && (mode == JOINT_MS || mode == JOINT_MIXED))
                {
                    faac_real enrgm = 0.25 * enrgs;
                    faac_real enrgside = 0.25 * enrgd;

                    if ((min(enrgl, enrgr) * thrmid) >= max(enrgm, enrgside))
                    {
                        if ((enrgm * thrmid * 2.0) >= efix ||
                            (enrgside * thrmid * 2.0) >= efix)
                        {
                            use_ms = 1;
                        }
                    }
                }

                // 3. Apply decision
                if (use_is)
                {
                    apply_is(coder + chn, coder + rch, s[chn], s[rch], sfcnt,
                             start_win, end_win, start_bin, end_bin, hcb, sf, pan,
                             enrgs, enrgd, efix);
                    channel[chn].msInfo.ms_used[sfcnt] = 0;
                }
                else if (use_ms)
                {
                    int phase = ((0.25 * enrgs * thrmid * 2.0) >= efix) ? MS_PH_IN : MS_PH_OUT;
                    apply_ms(channel + chn, s[chn], s[rch], sfcnt,
                             start_win, end_win, start_bin, end_bin, phase);
                }
                else
                {
                    apply_lr(channel + chn, s[chn], s[rch], sfcnt,
                             start_win, end_win, start_bin, end_bin,
                             enrgl, enrgr, thrside);
                }
                sfcnt++;
            }
            start_win = end_win;
        }
    skip:;
    }
}
