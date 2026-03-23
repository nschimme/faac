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

/* Perceptual Decision Thresholds (Empirical) */
#define MS_THR_BASE 1.09       /* ~0.75dB margin for M/S coding gain */
#define MS_THR_MAX  1.25       /* ~2.00dB clamp to prevent spatial 'breathing' */
#define SIDE_THR_MIN 0.1       /* -20dB noise floor for side-channel zeroing */
#define SIDE_THR_MAX 0.3       /* ~-10.5dB max side-zeroing for wide imaging */
#define IS_THR_COHERENCE 0.18  /* 18% energy leakage margin for IS (Iter 21) */
#define IS_PAN_MAX 30          /* AAC-LC max pan offset (scalefactor limit) */
#define ENERGY_EPSILON 1e-9    /* Silence floor to prevent div-by-zero */

enum { MS_PH_NONE, MS_PH_IN, MS_PH_OUT };

/**
 * apply_is - Applies Intensity Stereo transform to a scale factor band.
 */
static inline void apply_is(CoderInfo *cl, CoderInfo *cr,
                            faac_real * restrict sl0, faac_real * restrict sr0,
                            int sfcnt, int start_win, int end_win, int start_bin, int end_bin,
                            int hcb, int sf, int pan, faac_real enrgs_unnorm, faac_real enrgd_unnorm, faac_real efix)
{
    faac_real vfix;

    if (hcb == HCB_INTENSITY)
        vfix = FAAC_SQRT(efix / enrgs_unnorm);
    else
        vfix = FAAC_SQRT(efix / enrgd_unnorm);

    cl->sf[sfcnt] = sf;
    cr->sf[sfcnt] = -pan;
    cr->book[sfcnt] = hcb;

    /* Spectral folding: Map sum/diff of L/R to L, scaled to target energy. */
    for (int win = start_win; win < end_win; win++)
    {
        faac_real * restrict sl = sl0 + win * BLOCK_LEN_SHORT;
        const faac_real * restrict sr = sr0 + win * BLOCK_LEN_SHORT;
        for (int l = start_bin; l < end_bin; l++)
        {
            faac_real lx = sl[l];
            faac_real rx = sr[l];
            faac_real val = (hcb == HCB_INTENSITY) ? (lx + rx) : (lx - rx);
            sl[l] = val * vfix;
        }
    }
}

/**
 * apply_ms - Applies destructive M/S transform (phase-collapse).
 */
static inline void apply_ms(ChannelInfo *channel, faac_real * restrict sl0, faac_real * restrict sr0,
                            int sfcnt, int start_win, int end_win, int start_bin, int end_bin,
                            int phase)
{
    channel->msInfo.ms_used[sfcnt] = 1;

    /* M/S collapse: Force one channel to zero based on phase dominance to preserve bit reservoir. */
    for (int win = start_win; win < end_win; win++)
    {
        faac_real * restrict sl = sl0 + win * BLOCK_LEN_SHORT;
        faac_real * restrict sr = sr0 + win * BLOCK_LEN_SHORT;
        for (int l = start_bin; l < end_bin; l++)
        {
            faac_real lx = sl[l];
            faac_real rx = sr[l];
            if (phase == MS_PH_IN)
            {
                sl[l] = 0.5 * (lx + rx);
                sr[l] = 0.0;
            }
            else
            {
                sl[l] = 0.0;
                sr[l] = 0.5 * (lx - rx);
            }
        }
    }
}

/**
 * apply_lr - Fallback to L/R coding, potentially applying destructive side-channel zeroing.
 */
static inline void apply_lr(ChannelInfo *channel, faac_real * restrict sl0, faac_real * restrict sr0,
                            int sfcnt, int start_win, int end_win, int start_bin, int end_bin,
                            faac_real enrgl, faac_real enrgr, faac_real thrside)
{
    channel->msInfo.ms_used[sfcnt] = 0;

    /**
     * Side-channel zeroing (Masking):
     * If one channel is significantly dominant (L >> R or R >> L), we zero the quieter channel
     * to recover the bit reservoir for the dominant channel. This assumes the spatial imaging
     * is already collapsed to one side perceptually, so side-information bits are better
     * reinvested in core quantization.
     */
    if (min(enrgl, enrgr) <= (thrside * max(enrgl, enrgr)))
    {
        for (int win = start_win; win < end_win; win++)
        {
            faac_real * restrict sl = sl0 + win * BLOCK_LEN_SHORT;
            faac_real * restrict sr = sr0 + win * BLOCK_LEN_SHORT;
            if (enrgl < enrgr) {
                for (int l = start_bin; l < end_bin; l++) sl[l] = 0.0;
            } else {
                for (int l = start_bin; l < end_bin; l++) sr[l] = 0.0;
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
    static const faac_real thr075 = MS_THR_BASE - 1.0;
    static const faac_real thrmax = MS_THR_MAX - 1.0;
    static const faac_real sidemin = SIDE_THR_MIN;
    static const faac_real sidemax = SIDE_THR_MAX;
    static const faac_real isthrmax = M_SQRT2 - 1.0;
    faac_real thrmid, thrside;
    faac_real isthr;

    thrmid = 1.0;
    thrside = 0.0;
    isthr = 1.0;

    /* Define perceptual thresholds based on requested quality and mode. */
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
        /**
         * Intensity Stereo Phase-Coherence Threshold (IS_THR_COHERENCE):
         * This value represents the maximum allowable energy loss ratio when collapsing L/R to Mono.
         * For perfectly in-phase signals, (L+R)^2 == (|L|+|R|)^2. As phase diverges, (L+R)^2 decreases.
         * A threshold of 1.18 (at quality=1.0) allows for ~18% 'energy leakage' due to phase
         * misalignment before falling back to M/S or L/R. This was empirically found to be the
         * "optimal knee" in the quality-vs-bitrate curve during Iteration 21 of development.
         */
        isthr = IS_THR_COHERENCE / (quality * quality);
        if (isthr > isthrmax)
            isthr = isthrmax;

        isthr += 1.0;
        break;
    }

    if (mode == JOINT_MIXED)
    {
        /* Optimized Mixed Mode IS threshold (Iter 21) */
        faac_real m_isthr = IS_THR_COHERENCE / (quality * quality);
        if (m_isthr > isthrmax)
            m_isthr = isthrmax;
        isthr = m_isthr + 1.0;
    }

    // convert into energy
    thrmid *= thrmid;
    thrside *= thrside;
    isthr *= isthr;

    /* Initialize book and sf arrays for all present channels */
    for (chn = 0; chn < maxchan; chn++)
    {
        if (!channel[chn].present) continue;
        CoderInfo *cp = coder + chn;
        int bookcnt = 0;
        for (int group = 0; group < cp->groups.n; group++)
            for (int band = 0; band < cp->sfbn; band++)
            {
                cp->book[bookcnt] = HCB_NONE;
                cp->sf[bookcnt] = 0;
                bookcnt++;
            }
    }

    if (mode == JOINT_NONE)
        return;

    for (chn = 0; chn < maxchan; chn++)
    {
        if (!channel[chn].present || channel[chn].type != ELEMENT_CPE || !channel[chn].ch_is_left) continue;

        int rch = channel[chn].paired_ch;
        CoderInfo *cl = &coder[chn];
        CoderInfo *cr = &coder[rch];

        if (cl->block_type != cr->block_type || cl->groups.n != cr->groups.n) continue;

        channel[chn].common_window = 1;
        for (int cnt = 0; cnt < cl->groups.n; cnt++)
            if (cl->groups.len[cnt] != cr->groups.len[cnt])
            {
                channel[chn].common_window = 0;
                goto skip;
            }
        channel[rch].common_window = channel[chn].common_window;

        int frame_uses_ms = 0;
        int sfcnt = 0;
        int start_win = 0;
        for (int group = 0; group < cl->groups.n; group++)
        {
            int end_win = start_win + cl->groups.len[group];
            int sfmin = (cl->block_type == ONLY_SHORT_WINDOW) ? 1 : 8;

            for (int sfb = 0; sfb < sfmin; sfb++)
                channel[chn].msInfo.ms_used[sfcnt++] = 0;

            for (int sfb = sfmin; sfb < cl->sfbn; sfb++)
            {
                int start_bin = cl->sfb_offset[sfb];
                int end_bin = cl->sfb_offset[sfb + 1];
                faac_real enrgl = 0, enrgr = 0, enrgl_r = 0;

                /* Accumulate energies and cross-products across windows in the group. */
                for (int win = start_win; win < end_win; win++)
                {
                    const faac_real * restrict sl = s[chn] + win * BLOCK_LEN_SHORT;
                    const faac_real * restrict sr = s[rch] + win * BLOCK_LEN_SHORT;
                    for (int l = start_bin; l < end_bin; l++)
                    {
                        faac_real lx = sl[l];
                        faac_real rx = sr[l];
                        enrgl += lx * lx;
                        enrgr += rx * rx;
                        enrgl_r += lx * rx;
                    }
                }

                faac_real enrgs_unnorm = enrgl + enrgr + 2.0 * enrgl_r;
                faac_real enrgd_unnorm = enrgl + enrgr - 2.0 * enrgl_r;
                faac_real efix = enrgl + enrgr;
                faac_real enrgs_norm = 0.25 * enrgs_unnorm;
                faac_real enrgd_norm = 0.25 * enrgd_unnorm;
                int use_is = 0, use_ms = 0, hcb = HCB_NONE, sf = 0, pan = 0, phase = MS_PH_NONE;

                // 1. Evaluate Intensity Stereo
                if ((mode == JOINT_IS || mode == JOINT_MIXED) && efix > ENERGY_EPSILON)
                {
                    faac_real ethr = (FAAC_SQRT(enrgl) + FAAC_SQRT(enrgr));
                    ethr = ethr * ethr * (1.0 / isthr);

                    if (enrgs_unnorm >= ethr) hcb = HCB_INTENSITY;
                    else if (enrgd_unnorm >= ethr) hcb = HCB_INTENSITY2;

                    if (hcb != HCB_NONE)
                    {
                        /**
                         * Intensity Stereo Pan Calculation:
                         * The intensity magnitude is stored in the Left scalefactor, while the
                         * relative panning position (pan) is derived from the energy ratio.
                         * We clamp the pan to the valid AAC-LC scalefactor range to prevent
                         * bitstream overflows.
                         */
                        sf = FAAC_LRINT(FAAC_LOG10(enrgl / efix) * AAC_SF_STEP);
                        pan = FAAC_LRINT(FAAC_LOG10(enrgr / efix) * AAC_SF_STEP) - sf;
                        if (pan <= IS_PAN_MAX && pan >= -IS_PAN_MAX) use_is = 1;
                        else hcb = HCB_NONE;
                    }
                }

                /**
                 * 2. Evaluate M/S Coding:
                 * Mid/Side coding is beneficial if the sum (Mid) or difference (Side) signals
                 * have significantly lower energy than the individual L/R channels, or if
                 * the channels are highly correlated. We use 'destructive' phase-collapse
                 * logic where we only transmit the dominant phase to save bits.
                 */
                if (!use_is && (mode == JOINT_MS || mode == JOINT_MIXED) && efix > ENERGY_EPSILON)
                {
                    faac_real thr_m = enrgs_norm * thrmid * 2.0;
                    faac_real thr_d = enrgd_norm * thrmid * 2.0;
                    if ((min(enrgl, enrgr) * thrmid) >= max(enrgs_norm, enrgd_norm))
                    {
                        if (thr_m >= efix)
                        {
                            use_ms = 1;
                            phase = MS_PH_IN;
                        }
                        else if (thr_d >= efix)
                        {
                            use_ms = 1;
                            phase = MS_PH_OUT;
                        }
                    }
                }

                // 3. Apply Decision
                if (use_is)
                {
                    apply_is(cl, cr, s[chn], s[rch], sfcnt, start_win, end_win, start_bin, end_bin, hcb, sf, pan, enrgs_unnorm, enrgd_unnorm, efix);
                    channel[chn].msInfo.ms_used[sfcnt] = 0;
                }
                else if (use_ms)
                {
                    apply_ms(channel + chn, s[chn], s[rch], sfcnt, start_win, end_win, start_bin, end_bin, phase);
                    frame_uses_ms = 1;
                }
                else
                {
                    apply_lr(channel + chn, s[chn], s[rch], sfcnt, start_win, end_win, start_bin, end_bin, enrgl, enrgr, thrside);
                }
                sfcnt++;
            }
            start_win = end_win;
        }
        channel[chn].msInfo.is_present = frame_uses_ms;
        channel[rch].msInfo.is_present = frame_uses_ms;
    skip:;
    }
}
