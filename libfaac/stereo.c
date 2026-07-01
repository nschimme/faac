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
            s_out[l] = 0.0;
        s_out += BLOCK_LEN_SHORT;
    }
}

static inline void calculate_energies(const faac_real *sl_ptr, const faac_real *sr_ptr,
                                     int len, int wstart, int wend,
                                     faac_real *enrgl, faac_real *enrgr, faac_real *enrglr)
{
    int win;
    faac_real el = 0.0;
    faac_real er = 0.0;
    faac_real elr = 0.0;

    /* Accumulate energies and the cross-product term. Using three independent
     * accumulators allows the compiler to break the dependency chain and
     * better exploit instruction-level parallelism (ILP) on superscalar CPUs. */
    for (win = wstart; win < wend; win++)
    {
        int l;
        for (l = 0; l < len; l++)
        {
            faac_real lx = sl_ptr[l];
            faac_real rx = sr_ptr[l];

            el += lx * lx;
            er += rx * rx;
            elr += lx * rx;
        }
        sl_ptr += BLOCK_LEN_SHORT;
        sr_ptr += BLOCK_LEN_SHORT;
    }

    *enrgl = el;
    *enrgr = er;
    *enrglr = elr;
}

static inline void apply_ms(faac_real * restrict sl0, faac_real * restrict sr0,
                            int start, int len, int wstart, int wend, int in_phase)
{
    faac_real * restrict sl_out = sl0 + wstart * BLOCK_LEN_SHORT + start;
    faac_real * restrict sr_out = sr0 + wstart * BLOCK_LEN_SHORT + start;
    int win;

    /* Loop unswitching: hoisting the conditional check out of the window loop
     * eliminates redundant branching in the hot path. */
    if (in_phase)
    {
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

static inline void apply_is(faac_real * restrict sl0, faac_real * restrict sr0,
                            int start, int len, int wstart, int wend,
                            int in_phase, faac_real vfix)
{
    faac_real * restrict sl_out = sl0 + wstart * BLOCK_LEN_SHORT + start;
    faac_real * restrict sr_out = sr0 + wstart * BLOCK_LEN_SHORT + start;
    int win;

    /* Loop unswitching: hoisting the conditional check out of the window loop
     * eliminates redundant branching in the hot path. */
    if (in_phase)
    {
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
}


/* Unified CPE processing: handles MS, IS and L/R fallback in one pass.
 * Refactoring disparate modes into a single helper reduces maintenance
 * overhead and allows for shared energy calculation optimizations. */
static int process_cpe(CoderInfo * restrict cl, CoderInfo * restrict cr,
                       ChannelInfo * restrict channel,
                       faac_real * restrict sl0, faac_real * restrict sr0, int * restrict sfcnt,
                       int wstart, int wend, int mode,
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

    /* skip the first bands: MS and IS are typically restricted here */
    for (sfb = 0; sfb < sfmin; sfb++)
    {
        if (mode != JOINT_IS)
            channel->msInfo.ms_used[(*sfcnt)++] = 0;
        else
            (*sfcnt)++;
    }

    for (sfb = sfmin; sfb < cl->sfbn; sfb++)
    {
        int ms = 0;
        int start = sfb_offset[sfb];
        int end = sfb_offset[sfb + 1];
        int len = end - start;
        faac_real enrgs, enrgd, enrgl, enrgr, enrglr;
        faac_real efix;
        const faac_real * restrict sl_ptr = sl0 + wstart * BLOCK_LEN_SHORT + start;
        const faac_real * restrict sr_ptr = sr0 + wstart * BLOCK_LEN_SHORT + start;

        calculate_energies(sl_ptr, sr_ptr, len, wstart, wend, &enrgl, &enrgr, &enrglr);

        /* Derive sum/diff energies via |l+-r|^2 = l^2 + r^2 +- 2*l*r */
        enrgs = enrgl + enrgr + 2.0 * enrglr;
        enrgd = enrgl + enrgr - 2.0 * enrglr;
        /* float cancellation for L~=+-R can round these below zero; clamp before FAAC_SQRT (NaN guard) */
        if (enrgs < 0.0) enrgs = 0.0;
        if (enrgd < 0.0) enrgd = 0.0;

        efix = enrgl + enrgr;
        /* Skip completely silent bands first */
        if (efix <= 0.0)
        {
            if (mode != JOINT_IS)
                channel->msInfo.ms_used[(*sfcnt)++] = 0;
            else
                (*sfcnt)++;
            continue;
        }

        /* Intensity Stereo decision path */
        if (((mode == JOINT_IS) || (mode == JOINT_MIXED)) &&
            (sfb >= is_start_sfb) && (enrgl > 0.0) && (enrgr > 0.0))
        {
            int hcb = HCB_NONE;
            const faac_real step = 10/1.50515;
            faac_real ethr;
            faac_real vfix;

            /* IS optimization: (sqrt(L)+sqrt(R))^2 = L + R + 2*sqrt(L*R).
             * Expanding the form reduces the number of expensive FAAC_SQRT
             * calls per band while maintaining exact bit-identical output. */
            ethr = (enrgl + enrgr + 2.0 * FAAC_SQRT(enrgl * enrgr)) / isthr;

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
                    if (mode != JOINT_IS)
                        channel->msInfo.ms_used[(*sfcnt)++] = 0;
                    else
                        (*sfcnt)++;
                    continue;
                }
                if (pan < -30)
                {
                    cr->book[*sfcnt] = HCB_ZERO;
                    if (mode != JOINT_IS)
                        channel->msInfo.ms_used[(*sfcnt)++] = 0;
                    else
                        (*sfcnt)++;
                    continue;
                }
                cl->sf[*sfcnt] = sf;
                cr->sf[*sfcnt] = -pan;
                cr->book[*sfcnt] = hcb;
                if (mode != JOINT_IS)
                    channel->msInfo.ms_used[(*sfcnt)++] = 0;
                else
                    (*sfcnt)++;

                apply_is(sl0, sr0, start, len, wstart, wend,
                         hcb == HCB_INTENSITY, vfix);
                continue;
            }
        }

        /* Mid/Side decision path */
        if (mode == JOINT_MS || mode == JOINT_MIXED)
        {
            faac_real ms_enrgs = 0.25 * enrgs;
            faac_real ms_enrgd = 0.25 * enrgd;

            if ((min(enrgl, enrgr) * thrmid) >= max(ms_enrgs, ms_enrgd))
            {
                enum {PH_NONE, PH_IN, PH_OUT};
                int phase = PH_NONE;

                if ((ms_enrgs * thrmid * 2.0) >= efix)
                {
                    ms = 1;
                    phase = PH_IN;
                }
                else if ((ms_enrgd * thrmid * 2.0) >= efix)
                {
                    ms = 1;
                    phase = PH_OUT;
                }

                if (ms)
                {
                    msused = 1;
                    apply_ms(sl0, sr0, start, len, wstart, wend, phase == PH_IN);
                }
            }
        }

        /* Stereo thresholding: zero out nearly silent channels */
        if (!ms && (mode != JOINT_IS) && (min(enrgl, enrgr) <= (thrside * max(enrgl, enrgr))))
        {
            if (enrgl < enrgr)
                zero_channel(sl0, start, len, wstart, wend);
            else
                zero_channel(sr0, start, len, wstart, wend);
        }

        if (mode != JOINT_IS)
            channel->msInfo.ms_used[(*sfcnt)++] = ms;
        else
            (*sfcnt)++;
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

    /* all thresholds loosen as quality drops (divide by quality) and are
     * clamped so aggressive joint coding never kicks in at high quality */
    switch (mode)
    {
    case JOINT_MIXED:
        /* 0.85x tighter M/S than pure JOINT_MS (IS carries the rest); linear
         * isthr (not quality^2) keeps more phase at low quality */
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

        /* find the first SFB at/above 5.5 kHz; mixed() does IS from here up,
         * M/S below, where phase still carries the stereo image */
        if (mode == JOINT_MIXED)
        {
            int sfb;
            int mdctlen = (coder[chn].block_type == ONLY_SHORT_WINDOW)
                          ? (2 * BLOCK_LEN_SHORT) : (2 * BLOCK_LEN_LONG);
            /* cap the 5.5kHz IS floor at 70% of Nyquist: at low sample rates
             * 5.5kHz can exceed the top band and disable IS for the whole frame */
            int is_freq = 5500;
            int cap = (sampleRate * 7) / 20;
            if (is_freq > cap)
                is_freq = cap;

            is_start_sfb = coder[chn].sfbn;
            for (sfb = 0; sfb < coder[chn].sfbn; sfb++)
            {
                /* bin center -> Hz: offset * fs / mdctlen */
                int freq = (coder[chn].sfb_offset[sfb] * sampleRate) / mdctlen;
                if (freq >= is_freq)
                {
                    is_start_sfb = sfb;
                    break;
                }
            }
        }

        for (group = 0; group < coder[chn].groups.n; group++)
        {
            int end = start + coder[chn].groups.len[group];

            if (mode == JOINT_MS || mode == JOINT_IS || mode == JOINT_MIXED)
            {
                msused |= process_cpe(coder + chn, coder + rch, channel + chn,
                                      s[chn], s[rch], &sfcnt, start, end, mode,
                                      thrmid, thrside, isthr, is_start_sfb);
            }
            else
            {
                sfcnt += coder[chn].sfbn;
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
