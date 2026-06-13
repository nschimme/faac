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
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif


static void stereo(CoderInfo * restrict cl, CoderInfo * restrict cr,
                   faac_real * restrict sl0, faac_real * restrict sr0, int * restrict sfcnt,
                   int wstart, int wend, faac_real phthr
                  )
{
    int sfb;
    int sfmin;
    const int * restrict sfb_offset = cl->sfb_offset;

    if (!phthr)
        return;

    phthr = 1.0 / phthr;

    if (cl->block_type == ONLY_SHORT_WINDOW)
        sfmin = 1;
    else
        sfmin = 8;

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
        faac_real enrgs, enrgd, enrgl, enrgr, enrgs_minus_enrgd;
        int hcb = HCB_NONE;
        const faac_real step = 10/1.50515;
        faac_real ethr;
        faac_real vfix, efix;
        const faac_real * restrict sl_ptr = sl0 + wstart * BLOCK_LEN_SHORT + start;
        const faac_real * restrict sr_ptr = sr0 + wstart * BLOCK_LEN_SHORT + start;

        enrgl = enrgr = enrgs_minus_enrgd = 0.0;
        for (win = wstart; win < wend; win++)
        {
            int l;
            for (l = 0; l < len; l++)
            {
                faac_real lx = sl_ptr[l];
                faac_real rx = sr_ptr[l];

                enrgl += lx * lx;
                enrgr += rx * rx;
                enrgs_minus_enrgd += lx * rx;
            }
            sl_ptr += BLOCK_LEN_SHORT;
            sr_ptr += BLOCK_LEN_SHORT;
        }
        enrgs = enrgl + enrgr + 2.0 * enrgs_minus_enrgd;
        enrgd = enrgl + enrgr - 2.0 * enrgs_minus_enrgd;

        ethr = FAAC_SQRT(enrgl) + FAAC_SQRT(enrgr);
        ethr *= ethr;
        ethr *= phthr;
        efix = enrgl + enrgr;
        /* Skip completely silent bands: efix==0 makes ethr==0 so IS would
         * trigger spuriously, and vfix=sqrt(0/0) would be NaN. */
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
            /* If either channel is zero its log10 ratio is -inf; FAAC_LRINT
             * on -inf is undefined behaviour.  Skip to L/R coding instead. */
            if (enrgl == 0.0 || enrgr == 0.0)
            {
                (*sfcnt)++;
                continue;
            }
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

            if (hcb == HCB_INTENSITY)
            {
                faac_real * restrict sl_out = sl0 + wstart * BLOCK_LEN_SHORT + start;
                const faac_real * restrict sr_in = sr0 + wstart * BLOCK_LEN_SHORT + start;
                for (win = wstart; win < wend; win++)
                {
                    int l;
                    for (l = 0; l < len; l++)
                    {
                        sl_out[l] = (sl_out[l] + sr_in[l]) * vfix;
                    }
                    sl_out += BLOCK_LEN_SHORT;
                    sr_in += BLOCK_LEN_SHORT;
                }
            }
            else
            {
                faac_real * restrict sl_out = sl0 + wstart * BLOCK_LEN_SHORT + start;
                const faac_real * restrict sr_in = sr0 + wstart * BLOCK_LEN_SHORT + start;
                for (win = wstart; win < wend; win++)
                {
                    int l;
                    for (l = 0; l < len; l++)
                    {
                        sl_out[l] = (sl_out[l] - sr_in[l]) * vfix;
                    }
                    sl_out += BLOCK_LEN_SHORT;
                    sr_in += BLOCK_LEN_SHORT;
                }
            }
        }
        (*sfcnt)++;
    }
}

static void midside(CoderInfo * restrict coder, ChannelInfo * restrict channel,
                    faac_real * restrict sl0, faac_real * restrict sr0, int * restrict sfcnt,
                    int wstart, int wend,
                    faac_real thrmid, faac_real thrside
                   )
{
    int sfb;
    int sfmin;
    const int * restrict sfb_offset = coder->sfb_offset;

    if (coder->block_type == ONLY_SHORT_WINDOW)
        sfmin = 1;
    else
        sfmin = 8;

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
        faac_real enrgs, enrgd, enrgl, enrgr, enrgs_minus_enrgd;
        const faac_real * restrict sl_ptr = sl0 + wstart * BLOCK_LEN_SHORT + start;
        const faac_real * restrict sr_ptr = sr0 + wstart * BLOCK_LEN_SHORT + start;

        enrgl = enrgr = enrgs_minus_enrgd = 0.0;
        for (win = wstart; win < wend; win++)
        {
            int l;
            for (l = 0; l < len; l++)
            {
                faac_real lx = sl_ptr[l];
                faac_real rx = sr_ptr[l];

                enrgl += lx * lx;
                enrgr += rx * rx;
                enrgs_minus_enrgd += lx * rx;
            }
            sl_ptr += BLOCK_LEN_SHORT;
            sr_ptr += BLOCK_LEN_SHORT;
        }
        enrgs = 0.25 * (enrgl + enrgr + 2.0 * enrgs_minus_enrgd);
        enrgd = 0.25 * (enrgl + enrgr - 2.0 * enrgs_minus_enrgd);

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
                if (phase == PH_IN)
                {
                    faac_real * restrict sl_out = sl0 + wstart * BLOCK_LEN_SHORT + start;
                    faac_real * restrict sr_out = sr0 + wstart * BLOCK_LEN_SHORT + start;
                    for (win = wstart; win < wend; win++)
                    {
                        int l;
                        for (l = 0; l < len; l++)
                        {
                            sl_out[l] = 0.5 * (sl_out[l] + sr_out[l]);
                            sr_out[l] = 0.0;
                        }
                        sl_out += BLOCK_LEN_SHORT;
                        sr_out += BLOCK_LEN_SHORT;
                    }
                }
                else
                {
                    faac_real * restrict sl_out = sl0 + wstart * BLOCK_LEN_SHORT + start;
                    faac_real * restrict sr_out = sr0 + wstart * BLOCK_LEN_SHORT + start;
                    for (win = wstart; win < wend; win++)
                    {
                        int l;
                        for (l = 0; l < len; l++)
                        {
                            sr_out[l] = 0.5 * (sl_out[l] - sr_out[l]);
                            sl_out[l] = 0.0;
                        }
                        sl_out += BLOCK_LEN_SHORT;
                        sr_out += BLOCK_LEN_SHORT;
                    }
                }
            }
        }

        if (min(enrgl, enrgr) <= (thrside * max(enrgl, enrgr)))
        {
            if (enrgl < enrgr)
            {
                faac_real * restrict sl_out = sl0 + wstart * BLOCK_LEN_SHORT + start;
                for (win = wstart; win < wend; win++)
                {
                    int l;
                    for (l = 0; l < len; l++)
                        sl_out[l] = 0.0;
                    sl_out += BLOCK_LEN_SHORT;
                }
            }
            else
            {
                faac_real * restrict sr_out = sr0 + wstart * BLOCK_LEN_SHORT + start;
                for (win = wstart; win < wend; win++)
                {
                    int l;
                    for (l = 0; l < len; l++)
                        sr_out[l] = 0.0;
                    sr_out += BLOCK_LEN_SHORT;
                }
            }
        }

        channel->msInfo.ms_used[(*sfcnt)++] = ms;
    }
}

/* Per-band joint stereo: IS above is_start_sfb, M/S below, L/R fallback.
 * IS and M/S are mutually exclusive per scale factor band.
 * Returns 1 if any band was M/S coded (caller must then signal ms_used). */
static int mixed(CoderInfo * restrict cl, CoderInfo * restrict cr, ChannelInfo * restrict channel,
                 faac_real * restrict sl0, faac_real * restrict sr0, int * restrict sfcnt,
                 int wstart, int wend,
                 faac_real thrmid, faac_real thrside, faac_real isthr,
                 int is_start_sfb
                )
{
    int sfb;
    int sfmin;
    int msused = 0;
    const int * restrict sfb_offset = cl->sfb_offset;

    if (cl->block_type == ONLY_SHORT_WINDOW)
        sfmin = 1;
    else
        sfmin = 8;

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
        faac_real enrgs, enrgd, enrgl, enrgr, enrgs_minus_enrgd;
        faac_real efix;
        const faac_real * restrict sl_ptr = sl0 + wstart * BLOCK_LEN_SHORT + start;
        const faac_real * restrict sr_ptr = sr0 + wstart * BLOCK_LEN_SHORT + start;

        enrgl = enrgr = enrgs_minus_enrgd = 0.0;
        for (win = wstart; win < wend; win++)
        {
            int l;
            for (l = 0; l < len; l++)
            {
                faac_real lx = sl_ptr[l];
                faac_real rx = sr_ptr[l];

                enrgl += lx * lx;
                enrgr += rx * rx;
                enrgs_minus_enrgd += lx * rx;
            }
            sl_ptr += BLOCK_LEN_SHORT;
            sr_ptr += BLOCK_LEN_SHORT;
        }
        enrgs = enrgl + enrgr + 2.0 * enrgs_minus_enrgd;
        enrgd = enrgl + enrgr - 2.0 * enrgs_minus_enrgd;

        efix = enrgl + enrgr;
        /* Skip completely silent bands: efix==0 makes ethr==0 so IS would
         * trigger spuriously, and vfix=sqrt(0/0) would be NaN. */
        if (efix <= 0.0)
        {
            channel->msInfo.ms_used[(*sfcnt)++] = 0;
            continue;
        }

        /* If either channel is zero its log10 ratio is -inf; FAAC_LRINT
         * on -inf is undefined behaviour.  Skip IS for such bands. */
        if ((sfb >= is_start_sfb) && (enrgl > 0.0) && (enrgr > 0.0))
        {
            int hcb = HCB_NONE;
            const faac_real step = 10/1.50515;
            faac_real ethr;
            faac_real vfix;

            ethr = FAAC_SQRT(enrgl) + FAAC_SQRT(enrgr);
            ethr *= ethr;
            ethr /= isthr;

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

                if (hcb == HCB_INTENSITY)
                {
                    faac_real * restrict sl_out = sl0 + wstart * BLOCK_LEN_SHORT + start;
                    faac_real * restrict sr_out = sr0 + wstart * BLOCK_LEN_SHORT + start;
                    for (win = wstart; win < wend; win++)
                    {
                        int l;
                        for (l = 0; l < len; l++)
                        {
                            sl_out[l] = (sl_out[l] + sr_out[l]) * vfix;
                            sr_out[l] = 0.0;
                        }
                        sl_out += BLOCK_LEN_SHORT;
                        sr_out += BLOCK_LEN_SHORT;
                    }
                }
                else
                {
                    faac_real * restrict sl_out = sl0 + wstart * BLOCK_LEN_SHORT + start;
                    faac_real * restrict sr_out = sr0 + wstart * BLOCK_LEN_SHORT + start;
                    for (win = wstart; win < wend; win++)
                    {
                        int l;
                        for (l = 0; l < len; l++)
                        {
                            sl_out[l] = (sl_out[l] - sr_out[l]) * vfix;
                            sr_out[l] = 0.0;
                        }
                        sl_out += BLOCK_LEN_SHORT;
                        sr_out += BLOCK_LEN_SHORT;
                    }
                }
                continue;
            }
        }

        /* M/S decision: enrgs/enrgd are computed without the 0.5 mid/side
         * factor of midside(), hence the 0.25 energy compensation. */
        if ((min(enrgl, enrgr) * thrmid) >= max(enrgs * 0.25, enrgd * 0.25))
        {
            enum {PH_NONE, PH_IN, PH_OUT};
            int phase = PH_NONE;

            if ((enrgs * 0.25 * thrmid * 2.0) >= (enrgl + enrgr))
            {
                ms = 1;
                phase = PH_IN;
            }
            else if ((enrgd * 0.25 * thrmid * 2.0) >= (enrgl + enrgr))
            {
                ms = 1;
                phase = PH_OUT;
            }

            if (ms)
            {
                msused = 1;
                if (phase == PH_IN)
                {
                    faac_real * restrict sl_out = sl0 + wstart * BLOCK_LEN_SHORT + start;
                    faac_real * restrict sr_out = sr0 + wstart * BLOCK_LEN_SHORT + start;
                    for (win = wstart; win < wend; win++)
                    {
                        int l;
                        for (l = 0; l < len; l++)
                        {
                            sl_out[l] = 0.5 * (sl_out[l] + sr_out[l]);
                            sr_out[l] = 0.0;
                        }
                        sl_out += BLOCK_LEN_SHORT;
                        sr_out += BLOCK_LEN_SHORT;
                    }
                }
                else
                {
                    faac_real * restrict sl_out = sl0 + wstart * BLOCK_LEN_SHORT + start;
                    faac_real * restrict sr_out = sr0 + wstart * BLOCK_LEN_SHORT + start;
                    for (win = wstart; win < wend; win++)
                    {
                        int l;
                        for (l = 0; l < len; l++)
                        {
                            sr_out[l] = 0.5 * (sl_out[l] - sr_out[l]);
                            sl_out[l] = 0.0;
                        }
                        sl_out += BLOCK_LEN_SHORT;
                        sr_out += BLOCK_LEN_SHORT;
                    }
                }
            }
        }

        if (!ms && (min(enrgl, enrgr) <= (thrside * max(enrgl, enrgr))))
        {
            if (enrgl < enrgr)
            {
                faac_real * restrict sl_out = sl0 + wstart * BLOCK_LEN_SHORT + start;
                for (win = wstart; win < wend; win++)
                {
                    int l;
                    for (l = 0; l < len; l++)
                        sl_out[l] = 0.0;
                    sl_out += BLOCK_LEN_SHORT;
                }
            }
            else
            {
                faac_real * restrict sr_out = sr0 + wstart * BLOCK_LEN_SHORT + start;
                for (win = wstart; win < wend; win++)
                {
                    int l;
                    for (l = 0; l < len; l++)
                        sr_out[l] = 0.0;
                    sr_out += BLOCK_LEN_SHORT;
                }
            }
        }

        channel->msInfo.ms_used[(*sfcnt)++] = ms;
    }

    return msused;
}


void AACstereo(CoderInfo *coder,
               ChannelInfo *channel,
               faac_real *s[MAX_CHANNELS],
               int maxchan,
               faac_real quality,
               int mode,
               int sampleRate
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
    case JOINT_MIXED:
        thrmid = (thr075 * 0.85) / quality;
        if (thrmid > thrmax)
            thrmid = thrmax;
        thrside = sidemin / quality;
        if (thrside > sidemax)
            thrside = sidemax;
        thrmid += 1.0;

        isthr = 0.18 / quality;
        isthr += 1.0;
        if (isthr > isthrmax + 1.0)
            isthr = isthrmax + 1.0;
        break;
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
        isthr += 1.0;
        if (isthr > isthrmax + 1.0)
            isthr = isthrmax + 1.0;
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
        int is_start_sfb = 0;
        int msused = 0;

        if (!channel[chn].present)
            continue;
        if (!((channel[chn].type == ELEMENT_CPE) && (channel[chn].ch_is_left)))
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

        if (mode == JOINT_MIXED)
        {
            enum {IS_FREQ_LIMIT = 5500}; /* IS only above 5.5kHz */
            int sfb;
            int mdctlen = (coder[chn].block_type == ONLY_SHORT_WINDOW)
                          ? (2 * BLOCK_LEN_SHORT) : (2 * BLOCK_LEN_LONG);

            is_start_sfb = coder[chn].sfbn;
            for (sfb = 0; sfb < coder[chn].sfbn; sfb++)
            {
                int freq = (coder[chn].sfb_offset[sfb] * sampleRate) / mdctlen;
                if (freq >= IS_FREQ_LIMIT)
                {
                    is_start_sfb = sfb;
                    break;
                }
            }
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
            case JOINT_MIXED:
                msused |= mixed(coder + chn, coder + rch, channel + chn,
                                s[chn], s[rch], &sfcnt, start, end,
                                thrmid, thrside, isthr, is_start_sfb);
                break;
            default:
                sfcnt += coder[chn].sfbn;
                break;
            }
            start = end;
        }

        /* M/S bands are only decoded correctly when signalled via the
         * ms_used mask; without this the decoder treats them as L/R. */
        if ((mode == JOINT_MIXED) && msused)
        {
            channel[chn].msInfo.is_present = 1;
            channel[rch].msInfo.is_present = 1;
        }
        skip:;
    }
}
