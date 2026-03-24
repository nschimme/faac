/****************************************************************************
    Intensity Stereo and Mid/Side Coding

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
#define IS_PHASE_LEAK_LIMIT 0.18 /* 18% energy leakage limit for IS */
#define IS_PAN_MAX 30          /* AAC-LC scalefactor range limit; beyond this L/R energy ratio is too extreme for IS */

enum { MS_PH_NONE, MS_PH_IN, MS_PH_OUT };

/**
 * apply_is - Applies Intensity Stereo transform to a scale factor band.
 *
 * Spectral folding: Map sum/diff of L/R to L, scaled to target energy.
 */
static inline void apply_is(CoderInfo *cl, CoderInfo *cr,
                            faac_real * __restrict sl0, faac_real * __restrict sr0,
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

    for (int win = start_win; win < end_win; win++)
    {
        faac_real * __restrict sl = sl0 + win * BLOCK_LEN_SHORT;
        faac_real * __restrict sr = sr0 + win * BLOCK_LEN_SHORT;
        for (int l = start_bin; l < end_bin; l++)
        {
            faac_real lx = sl[l];
            faac_real rx = sr[l];
            faac_real val = (hcb == HCB_INTENSITY) ? (lx + rx) : (lx - rx);
            sl[l] = val * vfix;
            sr[l] = 0.0; /* Zero Right spectral data as per AAC-LC spec for IS */
        }
    }
}

/**
 * apply_ms - Applies destructive M/S transform (phase-collapse).
 *
 * M/S collapse: Force one channel to zero based on phase dominance to preserve bit reservoir and
 * avoid unnecessary computation.
 */
static inline void apply_ms(ChannelInfo *channel, faac_real * __restrict sl0, faac_real * __restrict sr0,
                            int sfcnt, int start_win, int end_win, int start_bin, int end_bin,
                            int phase)
{
    channel->msInfo.ms_used[sfcnt] = 1;

    for (int win = start_win; win < end_win; win++)
    {
        faac_real * __restrict sl = sl0 + win * BLOCK_LEN_SHORT;
        faac_real * __restrict sr = sr0 + win * BLOCK_LEN_SHORT;
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
 *
 * Side-channel zeroing (Masking):
 * If one channel is significantly dominant (L >> R or R >> L), we zero the quieter channel
 * to recover the bit reservoir for the dominant channel.
 */
static inline void apply_lr(ChannelInfo *channel, faac_real * __restrict sl0, faac_real * __restrict sr0,
                            int sfcnt, int start_win, int end_win, int start_bin, int end_bin,
                            faac_real enrgl, faac_real enrgr, faac_real thrside)
{
    channel->msInfo.ms_used[sfcnt] = 0;

    if (min(enrgl, enrgr) <= (thrside * max(enrgl, enrgr)))
    {
        for (int win = start_win; win < end_win; win++)
        {
            faac_real * __restrict sl = sl0 + win * BLOCK_LEN_SHORT;
            faac_real * __restrict sr = sr0 + win * BLOCK_LEN_SHORT;
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
               int mode,
               AACQuantCfg *aacquantCfg
              )
{
    int chn;
    faac_real quality = (faac_real)aacquantCfg->quality / DEFQUAL;

    /* 1. Fast State Reset (Mandatory for both Mono and Stereo to prevent stale data in BlocQuant) */
    for (chn = 0; chn < maxchan; chn++)
    {
        if (!channel[chn].present) continue;
        CoderInfo *cp = &coder[chn];
        int total_bands = cp->groups.n * cp->sfbn;
        int * __restrict book = cp->book;
        int * __restrict sf = cp->sf;
        for (int i = 0; i < total_bands; i++)
        {
            book[i] = HCB_NONE;
            sf[i] = 0;
        }
    }

    /* 2. Early Exit for Mono or Joint-Disabled streams to recover throughput */
    if (maxchan < 2 || mode == JOINT_NONE)
        return;

    /* 3. Perceptual Threshold Logic (Only for Stereo Joint modes) */
    static const faac_real thr075 = MS_THR_BASE - 1.0;
    static const faac_real thrmax = MS_THR_MAX - 1.0;
    static const faac_real sidemin = SIDE_THR_MIN;
    static const faac_real sidemax = SIDE_THR_MAX;
    static const faac_real isthrmax = M_SQRT2 - 1.0;
    faac_real thrmid = 1.0, thrside = 0.0, isthr = 1.0;

    if (mode == JOINT_MS || mode == JOINT_MIXED)
    {
        thrmid = thr075 / quality;
        if (thrmid > thrmax)
            thrmid = thrmax;

        thrside = sidemin / quality;
        if (thrside > sidemax)
            thrside = sidemax;

        thrmid += 1.0;
    }

    if (mode == JOINT_IS || mode == JOINT_MIXED)
    {
        /**
         * Intensity Stereo Phase-Coherence Threshold (IS_PHASE_LEAK_LIMIT):
         * Margin allowed for phase misalignment before falling back to L/R tools.
         */
        faac_real m_isthr = IS_PHASE_LEAK_LIMIT / (quality * quality);
        if (m_isthr > isthrmax)
            m_isthr = isthrmax;
        isthr = m_isthr + 1.0;
    }

    /* Convert perceptual thresholds to energy ratios. */
    thrmid *= thrmid;
    thrside *= thrside;
    isthr *= isthr;

    /* 4. Unified Decision Loop (Per-Band Tool Selection) */
    for (chn = 0; chn < maxchan; chn++)
    {
        if (!channel[chn].present || channel[chn].type != ELEMENT_CPE || !channel[chn].ch_is_left)
            continue;

        int rch = channel[chn].paired_ch;
        CoderInfo *cl = &coder[chn];
        CoderInfo *cr = &coder[rch];

        /* Reset IS presence markers early to prevent stale state on skip. */
        channel[chn].msInfo.is_present = 0;
        channel[rch].msInfo.is_present = 0;

        channel[rch].common_window = channel[chn].common_window = 0;

        if (cl->block_type != cr->block_type || cl->groups.n != cr->groups.n)
            continue;

        for (int cnt = 0; cnt < cl->groups.n; cnt++)
            if (cl->groups.len[cnt] != cr->groups.len[cnt])
                goto skip;

        channel[rch].common_window = channel[chn].common_window = 1;

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
                    const faac_real * __restrict sl = s[chn] + win * BLOCK_LEN_SHORT;
                    const faac_real * __restrict sr = s[rch] + win * BLOCK_LEN_SHORT;
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

                /* Per-band energy equivalent of the noisefloor RMS threshold:
                * noisefloor is per-sample RMS, so (noisefloor * noisefloor * num_samples)
                * gives the total band energy below which joint coding is pointless. */
                faac_real silence_floor = (aacquantCfg->noisefloor * aacquantCfg->noisefloor) * (end_bin - start_bin) * (end_win - start_win);

                /**
                 * 1. Evaluate M/S Coding:
                 * Mid/Side coding is prioritized over IS to preserve inter-channel phase.
                 * Benefit is calculated if the sum (Mid) or difference (Side) signals
                 * have significantly lower energy than L/R or are highly correlated.
                 */
                if ((mode == JOINT_MS || mode == JOINT_MIXED) && efix > silence_floor)
                {
                    faac_real mid_w = enrgs_norm * thrmid * 2.0;
                    faac_real side_w = enrgd_norm * thrmid * 2.0;
                    if ((min(enrgl, enrgr) * thrmid) >= max(enrgs_norm, enrgd_norm))
                    {
                        if (mid_w >= efix)
                        {
                            use_ms = 1;
                            phase = MS_PH_IN;
                        }
                        else if (side_w >= efix)
                        {
                            use_ms = 1;
                            phase = MS_PH_OUT;
                        }
                    }
                }

                /**
                 * 2. Evaluate Intensity Stereo:
                 * Only if M/S is not chosen. IS provides maximum bit recovery but collapses
                 * the phase image entirely.
                 */
                if (!use_ms
                    && efix > silence_floor
                    && (mode == JOINT_IS
                        || (mode == JOINT_MIXED
                            && sfb >= aacquantCfg->is_sfb_start)))
                {
                    faac_real ethr = (FAAC_SQRT(enrgl) + FAAC_SQRT(enrgr));
                    ethr = ethr * ethr * (1.0 / isthr);

                    if (enrgs_unnorm >= ethr) hcb = HCB_INTENSITY;
                    else if (enrgd_unnorm >= ethr) hcb = HCB_INTENSITY2;

                    if (hcb != HCB_NONE)
                    {
                        /**
                         * Intensity Stereo Pan Calculation:
                         * Derived from energy ratio. Magnitude in Left, Pan in Right.
                         */
                        sf = FAAC_LRINT(FAAC_LOG10(enrgl / efix) * AAC_SF_STEP);
                        pan = FAAC_LRINT(FAAC_LOG10(enrgr / efix) * AAC_SF_STEP) - sf;
                        if (pan <= IS_PAN_MAX && pan >= -IS_PAN_MAX)
                            use_is = 1;
                        else
                            hcb = HCB_NONE; /* pan out of range; apply_lr will zero the quieter channel via thrside */
                    }
                }

                /* 3. Apply Decision */
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
                    /* Only apply thrside zeroing if MS was evaluated and rejected;
                    * for bands that never qualified for joint coding, leave them untouched. */
                    apply_lr(channel + chn, s[chn], s[rch], sfcnt, start_win, end_win, start_bin, end_bin, enrgl, enrgr, use_ms ? 0.0 : thrside);
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
