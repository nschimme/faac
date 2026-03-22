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

#ifndef min
#define min(a,b) ( (a) < (b) ? (a) : (b) )
#endif

#ifndef max
#define max(a,b) ( (a) > (b) ? (a) : (b) )
#endif

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
        isthr = 0.18 / (quality * quality);
        if (isthr > isthrmax)
            isthr = isthrmax;
        isthr += 1.0;
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
                faac_real sum, diff;

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

                        enrgl += lx * lx;
                        enrgr += rx * rx;

                        sum = lx + rx;
                        diff = lx - rx;
                        enrgs += sum * sum;
                        enrgd += diff * diff;
                    }
                }

                int use_is = 0;
                int use_ms = 0;
                int hcb = HCB_NONE;

                // Decision logic
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
                        faac_real efix = enrgl + enrgr;
                        const faac_real step = 10 / 1.50515;
                        int sf = FAAC_LRINT(FAAC_LOG10(enrgl / efix) * step);
                        int pan = FAAC_LRINT(FAAC_LOG10(enrgr / efix) * step) - sf;

                        if (pan <= 30 && pan >= -30)
                        {
                            use_is = 1;
                        }
                        else
                        {
                            hcb = HCB_NONE;
                        }
                    }
                }

                if (!use_is && (mode == JOINT_MS || mode == JOINT_MIXED))
                {
                    // M/S decision
                    // M = 0.5*(L+R), S = 0.5*(L-R) -> energies are 0.25 of enrgs/enrgd
                    faac_real enrgm = 0.25 * enrgs;
                    faac_real enrgside = 0.25 * enrgd;

                    if ((min(enrgl, enrgr) * thrmid) >= max(enrgm, enrgside))
                    {
                        if ((enrgm * thrmid * 2.0) >= (enrgl + enrgr) ||
                            (enrgside * thrmid * 2.0) >= (enrgl + enrgr))
                        {
                            use_ms = 1;
                        }
                    }
                }

                if (use_is)
                {
                    faac_real efix = enrgl + enrgr;
                    faac_real vfix;
                    const faac_real step = 10 / 1.50515;
                    int sf = FAAC_LRINT(FAAC_LOG10(enrgl / efix) * step);
                    int pan = FAAC_LRINT(FAAC_LOG10(enrgr / efix) * step) - sf;

                    if (hcb == HCB_INTENSITY)
                        vfix = FAAC_SQRT(efix / enrgs);
                    else
                        vfix = FAAC_SQRT(efix / enrgd);

                    coder[chn].sf[sfcnt] = sf;
                    coder[rch].sf[sfcnt] = -pan;
                    coder[rch].book[sfcnt] = hcb;
                    channel[chn].msInfo.ms_used[sfcnt] = 0;

                    for (win = start_win; win < end_win; win++)
                    {
                        faac_real *sl = s[chn] + win * BLOCK_LEN_SHORT;
                        faac_real *sr = s[rch] + win * BLOCK_LEN_SHORT;
                        for (l = start_bin; l < end_bin; l++)
                        {
                            if (hcb == HCB_INTENSITY)
                                sum = sl[l] + sr[l];
                            else
                                sum = sl[l] - sr[l];
                            sl[l] = sum * vfix;
                        }
                    }
                }
                else if (use_ms)
                {
                    int phase;
                    faac_real enrgm = 0.25 * enrgs;

                    if ((enrgm * thrmid * 2.0) >= (enrgl + enrgr))
                        phase = 1; // IN
                    else
                        phase = 2; // OUT

                    channel[chn].msInfo.ms_used[sfcnt] = 1;

                    for (win = start_win; win < end_win; win++)
                    {
                        faac_real *sl = s[chn] + win * BLOCK_LEN_SHORT;
                        faac_real *sr = s[rch] + win * BLOCK_LEN_SHORT;
                        for (l = start_bin; l < end_bin; l++)
                        {
                            if (phase == 1)
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
                else
                {
                    // L/R or phase-collapse
                    channel[chn].msInfo.ms_used[sfcnt] = 0;
                    if (mode == JOINT_MS || mode == JOINT_MIXED)
                    {
                        if (min(enrgl, enrgr) <= (thrside * max(enrgl, enrgr)))
                        {
                            for (win = start_win; win < end_win; win++)
                            {
                                faac_real *sl = s[chn] + win * BLOCK_LEN_SHORT;
                                faac_real *sr = s[rch] + win * BLOCK_LEN_SHORT;
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
                }
                sfcnt++;
            }
            start_win = end_win;
        }
    skip:;
    }
}
