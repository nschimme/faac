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

static inline void zero_channel(faac_real * restrict s0, int start, int len,
                                int wstart, int wend)
{
    faac_real * restrict s_out = s0 + wstart * BLOCK_LEN_SHORT + start;
    int win;

    for (win = wstart; win < wend; win++)
    {
        int l;
        for (l = 0; l < len; l++)
        {
            s_out[l] = 0.0;
        }
        s_out += BLOCK_LEN_SHORT;
    }
}

static inline void apply_ms_mono(faac_real * restrict sl0, faac_real * restrict sr0,
                                 int start, int len, int wstart, int wend, int in_phase)
{
    faac_real * restrict sl_out = sl0 + wstart * BLOCK_LEN_SHORT + start;
    faac_real * restrict sr_out = sr0 + wstart * BLOCK_LEN_SHORT + start;
    int win;

    for (win = wstart; win < wend; win++)
    {
        int l;
        if (in_phase)
        {
            for (l = 0; l < len; l++)
            {
                sl_out[l] = 0.5 * (sl_out[l] + sr_out[l]);
                sr_out[l] = 0.0;
            }
        }
        else
        {
            for (l = 0; l < len; l++)
            {
                sr_out[l] = 0.5 * (sl_out[l] - sr_out[l]);
                sl_out[l] = 0.0;
            }
        }
        sl_out += BLOCK_LEN_SHORT;
        sr_out += BLOCK_LEN_SHORT;
    }
}

static inline void apply_ms_true(faac_real * restrict sl0, faac_real * restrict sr0,
                                 int start, int len, int wstart, int wend)
{
    faac_real * restrict sl_out = sl0 + wstart * BLOCK_LEN_SHORT + start;
    faac_real * restrict sr_out = sr0 + wstart * BLOCK_LEN_SHORT + start;
    int win;

    for (win = wstart; win < wend; win++)
    {
        int l;
        for (l = 0; l < len; l++)
        {
            faac_real m = 0.5 * (sl_out[l] + sr_out[l]);
            faac_real s = 0.5 * (sl_out[l] - sr_out[l]);
            sl_out[l] = m;
            sr_out[l] = s;
        }
        sl_out += BLOCK_LEN_SHORT;
        sr_out += BLOCK_LEN_SHORT;
    }
}

static inline void apply_is(faac_real * restrict sl0, faac_real * restrict sr0,
                            int start, int len, int wstart, int wend,
                            int in_phase, faac_real vfix)
{
    faac_real * restrict sl_out = sl0 + wstart * BLOCK_LEN_SHORT + start;
    faac_real * restrict sr_out = sr0 + wstart * BLOCK_LEN_SHORT + start;
    int win;

    for (win = wstart; win < wend; win++)
    {
        int l;
        if (in_phase)
        {
            for (l = 0; l < len; l++)
            {
                sl_out[l] = (sl_out[l] + sr_out[l]) * vfix;
                sr_out[l] = 0.0;
            }
        }
        else
        {
            for (l = 0; l < len; l++)
            {
                sl_out[l] = (sl_out[l] - sr_out[l]) * vfix;
                sr_out[l] = 0.0;
            }
        }
        sl_out += BLOCK_LEN_SHORT;
        sr_out += BLOCK_LEN_SHORT;
    }
}

static void stereo(CoderInfo * restrict cl, CoderInfo * restrict cr,
                   faac_real * restrict sl0, faac_real * restrict sr0, int * restrict sfcnt,
                   int wstart, int wend, faac_real phthr
                  )
{
    int sfb;
    int sfmin;
    const int * restrict sfb_offset = cl->sfb_offset;

    if (!phthr)
    {
        return;
    }

    phthr = 1.0 / phthr;

    if (cl->block_type == ONLY_SHORT_WINDOW)
    {
        sfmin = 1;
    }
    else
    {
        sfmin = 8;
    }

    for (sfb = 0; sfb < sfmin; sfb++)
    {
        (*sfcnt)++;
    }

    for (sfb = sfmin; sfb < cl->sfbn; sfb++)
    {
        int win;
        int start = sfb_offset[sfb];
        int end = sfb_offset[sfb + 1];
        int len = end - start;
        faac_real enrgl, enrgr, enrglr, enrgs, enrgd, ethr, efix, vfix;
        int hcb = HCB_NONE;
        const faac_real step = 10/1.50515;
        const faac_real * restrict sl_ptr = sl0 + wstart * BLOCK_LEN_SHORT + start;
        const faac_real * restrict sr_ptr = sr0 + wstart * BLOCK_LEN_SHORT + start;

        enrgl = enrgr = enrglr = 0.0;
        for (win = wstart; win < wend; win++)
        {
            int l;
            for (l = 0; l < len; l++)
            {
                faac_real lx = sl_ptr[l];
                faac_real rx = sr_ptr[l];

                enrgl += lx * lx;
                enrgr += rx * rx;
                enrglr += lx * rx;
            }
            sl_ptr += BLOCK_LEN_SHORT;
            sr_ptr += BLOCK_LEN_SHORT;
        }

        enrgs = enrgl + enrgr + 2.0 * enrglr;
        enrgd = enrgl + enrgr - 2.0 * enrglr;

        if (enrgs < 0.0)
        {
            enrgs = 0.0;
        }
        if (enrgd < 0.0)
        {
            enrgd = 0.0;
        }

        ethr = (FAAC_SQRT(enrgl) + FAAC_SQRT(enrgr));
        ethr = ethr * ethr * phthr;
        efix = enrgl + enrgr;

        if (efix <= 0.0)
        {
            (*sfcnt)++;
            continue;
        }

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
            if (enrgl == 0.0 || enrgr == 0.0)
            {
                (*sfcnt)++;
                continue;
            }

            int sf = FAAC_LRINT(FAAC_LOG10(enrgl / efix) * step);
            int pan = FAAC_LRINT(FAAC_LOG10(enrgr / efix) * step) - sf;

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

            apply_is(sl0, sr0, start, len, wstart, wend, hcb == HCB_INTENSITY, vfix);
        }
        (*sfcnt)++;
    }
}

static void midside(CoderInfo * restrict coder, ChannelInfo * restrict channel,
                    faac_real * restrict sl0, faac_real * restrict sr0, int * restrict sfcnt,
                    int wstart, int wend,
                    faac_real thrmid, faac_real thrside, faac_real coll_thr
                   )
{
    int sfb;
    int sfmin;
    const int * restrict sfb_offset = coder->sfb_offset;

    if (coder->block_type == ONLY_SHORT_WINDOW)
    {
        sfmin = 1;
    }
    else
    {
        sfmin = 8;
    }

    for (sfb = 0; sfb < sfmin; sfb++)
    {
        channel->msInfo.ms_used[(*sfcnt)++] = 0;
    }

    for (sfb = sfmin; sfb < coder->sfbn; sfb++)
    {
        int ms = 0;
        int win;
        int start = sfb_offset[sfb];
        int end = sfb_offset[sfb + 1];
        int len = end - start;
        faac_real enrgl, enrgr, enrglr, enrgs, enrgd;
        const faac_real * restrict sl_ptr = sl0 + wstart * BLOCK_LEN_SHORT + start;
        const faac_real * restrict sr_ptr = sr0 + wstart * BLOCK_LEN_SHORT + start;

        enrgl = enrgr = enrglr = 0.0;
        for (win = wstart; win < wend; win++)
        {
            int l;
            for (l = 0; l < len; l++)
            {
                faac_real lx = sl_ptr[l];
                faac_real rx = sr_ptr[l];

                enrgl += lx * lx;
                enrgr += rx * rx;
                enrglr += lx * rx;
            }
            sl_ptr += BLOCK_LEN_SHORT;
            sr_ptr += BLOCK_LEN_SHORT;
        }

        /* 0.25 = the (1/2)^2 from the mid/side half-scaling */
        enrgs = 0.25 * (enrgl + enrgr + 2.0 * enrglr);
        enrgd = 0.25 * (enrgl + enrgr - 2.0 * enrglr);

        if ((min(enrgl, enrgr) * thrmid) >= max(enrgs, enrgd))
        {
            ms = 1;
            if (enrgs > (enrgd * coll_thr))
            {
                apply_ms_mono(sl0, sr0, start, len, wstart, wend, 1);
            }
            else if (enrgd > (enrgs * coll_thr))
            {
                apply_ms_mono(sl0, sr0, start, len, wstart, wend, 0);
            }
            else
            {
                apply_ms_true(sl0, sr0, start, len, wstart, wend);
            }
        }

        if (!ms && (min(enrgl, enrgr) <= (thrside * max(enrgl, enrgr))))
        {
            if (enrgl < enrgr)
            {
                zero_channel(sl0, start, len, wstart, wend);
            }
            else
            {
                zero_channel(sr0, start, len, wstart, wend);
            }
        }

        channel->msInfo.ms_used[(*sfcnt)++] = ms;
    }
}

static int mixed(CoderInfo * restrict cl, CoderInfo * restrict cr, ChannelInfo * restrict channel,
                 faac_real * restrict sl0, faac_real * restrict sr0, int * restrict sfcnt,
                 int wstart, int wend,
                 faac_real thrmid, faac_real thrside, faac_real isthr,
                 int is_start_sfb, faac_real coll_thr
                )
{
    int sfb;
    int sfmin;
    int msused = 0;
    const int * restrict sfb_offset = cl->sfb_offset;

    if (cl->block_type == ONLY_SHORT_WINDOW)
    {
        sfmin = 1;
    }
    else
    {
        sfmin = 8;
    }

    for (sfb = 0; sfb < sfmin; sfb++)
    {
        channel->msInfo.ms_used[(*sfcnt)++] = 0;
    }

    for (sfb = sfmin; sfb < cl->sfbn; sfb++)
    {
        int ms = 0;
        int win;
        int start = sfb_offset[sfb];
        int end = sfb_offset[sfb + 1];
        int len = end - start;
        faac_real enrgl, enrgr, enrglr, enrgs, enrgd, efix;
        const faac_real * restrict sl_ptr = sl0 + wstart * BLOCK_LEN_SHORT + start;
        const faac_real * restrict sr_ptr = sr0 + wstart * BLOCK_LEN_SHORT + start;

        enrgl = enrgr = enrglr = 0.0;
        for (win = wstart; win < wend; win++)
        {
            int l;
            for (l = 0; l < len; l++)
            {
                faac_real lx = sl_ptr[l];
                faac_real rx = sr_ptr[l];

                enrgl += lx * lx;
                enrgr += rx * rx;
                enrglr += lx * rx;
            }
            sl_ptr += BLOCK_LEN_SHORT;
            sr_ptr += BLOCK_LEN_SHORT;
        }

        enrgs = enrgl + enrgr + 2.0 * enrglr;
        enrgd = enrgl + enrgr - 2.0 * enrglr;

        if (enrgs < 0.0)
        {
            enrgs = 0.0;
        }
        if (enrgd < 0.0)
        {
            enrgd = 0.0;
        }

        efix = enrgl + enrgr;
        if (efix <= 0.0)
        {
            channel->msInfo.ms_used[(*sfcnt)++] = 0;
            continue;
        }

        if ((sfb >= is_start_sfb) && (enrgl > 0.0) && (enrgr > 0.0))
        {
            int hcb = HCB_NONE;
            const faac_real step = 10/1.50515;
            faac_real ethr, vfix;

            ethr = (FAAC_SQRT(enrgl) + FAAC_SQRT(enrgr));
            ethr = ethr * ethr / isthr;

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
                int pan = FAAC_LRINT(FAAC_LOG10(enrgr / efix) * step) - sf;

                if (pan > 30)
                {
                    cl->book[*sfcnt] = HCB_ZERO;
                    channel->msInfo.ms_used[(*sfcnt)++] = 0;
                    continue;
                }
                if (pan < -30)
                {
                    cr->book[*sfcnt] = HCB_ZERO;
                    channel->msInfo.ms_used[(*sfcnt)++] = 0;
                    continue;
                }

                cl->sf[*sfcnt] = sf;
                cr->sf[*sfcnt] = -pan;
                cr->book[*sfcnt] = hcb;
                channel->msInfo.ms_used[(*sfcnt)++] = 0;

                apply_is(sl0, sr0, start, len, wstart, wend, hcb == HCB_INTENSITY, vfix);
                continue;
            }
        }

        /* M/S decision: Quality-adaptive hybrid transform. */
        if ((min(enrgl, enrgr) * thrmid) >= max(enrgs * 0.25, enrgd * 0.25))
        {
            ms = 1;
            msused = 1;
            if (enrgs * 0.25 > (enrgd * 0.25 * coll_thr))
            {
                apply_ms_mono(sl0, sr0, start, len, wstart, wend, 1);
            }
            else if (enrgd * 0.25 > (enrgs * 0.25 * coll_thr))
            {
                apply_ms_mono(sl0, sr0, start, len, wstart, wend, 0);
            }
            else
            {
                apply_ms_true(sl0, sr0, start, len, wstart, wend);
            }
        }

        if (!ms && (min(enrgl, enrgr) <= (thrside * max(enrgl, enrgr))))
        {
            if (enrgl < enrgr)
            {
                zero_channel(sl0, start, len, wstart, wend);
            }
            else
            {
                zero_channel(sr0, start, len, wstart, wend);
            }
        }

        channel->msInfo.ms_used[(*sfcnt)++] = ms;
    }

    return msused;
}


void AACstereo(CoderInfo *coder, ChannelInfo *channel, faac_real *s[MAX_CHANNELS], int maxchan,
               faac_real quality, int mode, int sampleRate)
{
    int chn;
    static const faac_real thr075 = 1.09 - 1.0;
    static const faac_real thrmax = 1.25 - 1.0;
    static const faac_real isthrmax = M_SQRT2 - 1.0;
    faac_real sidemin = 0.05;
    faac_real sidemax = 0.2;
    faac_real thrmid, thrside, isthr, coll_thr;
    int is_freq;

    /* Piecewise Adaptive Curves to balance stereo coherence and MOS loss. */
    if (quality <= 0.5)
    {
        coll_thr = 1.0;
        is_freq = 5500;
    }
    else if (quality >= 4.0)
    {
        coll_thr = 60.0;
        is_freq = 10000;
    }
    else if (quality >= 1.0)
    {
        coll_thr = 25.0 + (35.0 / 3.0) * (quality - 1.0);
        is_freq = 7500 + FAAC_LRINT(833.0 * (quality - 1.0));
    }
    else
    {
        coll_thr = 1.0 + 48.0 * (quality - 0.5);
        is_freq = 5500 + FAAC_LRINT(4000.0 * (quality - 0.5));
    }

    thrmid = 1.0;
    thrside = 0.0;
    isthr = 1.0;

    switch (mode)
    {
    case JOINT_MIXED:
        thrmid = (thr075 * 0.85) / quality;
        if (thrmid > thrmax)
        {
            thrmid = thrmax;
        }
        thrside = sidemin / quality;
        if (thrside > sidemax)
        {
            thrside = sidemax;
        }
        thrmid += 1.0;
        isthr = 0.18 / quality + 1.0;
        if (isthr > isthrmax + 1.0)
        {
            isthr = isthrmax + 1.0;
        }
        break;
    case JOINT_MS:
        thrmid = thr075 / quality;
        if (thrmid > thrmax)
        {
            thrmid = thrmax;
        }
        thrside = sidemin / quality;
        if (thrside > sidemax)
        {
            thrside = sidemax;
        }
        thrmid += 1.0;
        break;
    case JOINT_IS:
        isthr = 0.18 / (quality * quality) + 1.0;
        if (isthr > isthrmax + 1.0)
        {
            isthr = isthrmax + 1.0;
        }
        break;
    }

    thrmid *= thrmid;
    thrside *= thrside;
    isthr *= isthr;

    for (chn = 0; chn < maxchan; chn++)
    {
        CoderInfo *cp = coder + chn;
        if (!channel[chn].present)
        {
            continue;
        }
        for (int i = 0; i < cp->groups.n * cp->sfbn; i++)
        {
            cp->book[i] = HCB_NONE;
            cp->sf[i] = 0;
        }
    }

    for (chn = 0; chn < maxchan; chn++)
    {
        int rch, sfcnt = 0, start = 0, is_start_sfb = 0, msused = 0;
        if (!channel[chn].present || !((channel[chn].type == ELEMENT_CPE) && (channel[chn].ch_is_left)))
        {
            continue;
        }
        rch = channel[chn].paired_ch;
        channel[chn].common_window = 0;
        channel[chn].msInfo.is_present = 0;
        channel[rch].msInfo.is_present = 0;

        if (coder[chn].block_type != coder[rch].block_type || coder[chn].groups.n != coder[rch].groups.n)
        {
            continue;
        }

        channel[chn].common_window = 1;
        for (int i = 0; i < coder[chn].groups.n; i++)
        {
            if (coder[chn].groups.len[i] != coder[rch].groups.len[i])
            {
                channel[chn].common_window = 0;
                goto skip;
            }
        }

        if (mode == JOINT_MS)
        {
            channel[chn].msInfo.is_present = 1;
            channel[rch].msInfo.is_present = 1;
        }

        if (mode == JOINT_MIXED)
        {
            int mdctlen = (coder[chn].block_type == ONLY_SHORT_WINDOW) ? (2 * BLOCK_LEN_SHORT) : (2 * BLOCK_LEN_LONG);
            int cap = (sampleRate * 7) / 20;
            if (is_freq > cap)
            {
                is_freq = cap;
            }
            is_start_sfb = coder[chn].sfbn;
            for (int sfb = 0; sfb < coder[chn].sfbn; sfb++)
            {
                if ((coder[chn].sfb_offset[sfb] * sampleRate) / mdctlen >= is_freq)
                {
                    is_start_sfb = sfb;
                    break;
                }
            }
        }

        for (int group = 0; group < coder[chn].groups.n; group++)
        {
            int end = start + coder[chn].groups.len[group];
            if (mode == JOINT_MS)
            {
                midside(coder + chn, channel + chn, s[chn], s[rch], &sfcnt, start, end, thrmid, thrside, coll_thr);
            }
            else if (mode == JOINT_IS)
            {
                stereo(coder + chn, coder + rch, s[chn], s[rch], &sfcnt, start, end, isthr);
            }
            else if (mode == JOINT_MIXED)
            {
                msused |= mixed(coder + chn, coder + rch, channel + chn, s[chn], s[rch], &sfcnt, start, end, thrmid, thrside, isthr, is_start_sfb, coll_thr);
            }
            else
            {
                sfcnt += coder[chn].sfbn;
            }
            start = end;
        }

        if ((mode == JOINT_MIXED) && msused)
        {
            channel[chn].msInfo.is_present = 1;
            channel[rch].msInfo.is_present = 1;
        }
        skip:;
    }
}
