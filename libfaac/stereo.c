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

/* Quality-Adaptive Hybrid M/S transform. side' = alpha * side.
 * Preserves spatial image while managing bit budget. */
static inline void apply_ms(faac_real * restrict sl, faac_real * restrict sr,
                            int start, int len, int wstart, int wend,
                            int in_phase, faac_real alpha)
{
    sl += wstart * BLOCK_LEN_SHORT + start;
    sr += wstart * BLOCK_LEN_SHORT + start;

    for (int win = wstart; win < wend; win++)
    {
        if (in_phase)
            for (int l = 0; l < len; l++)
            {
                faac_real m = 0.5 * (sl[l] + sr[l]);
                faac_real s = 0.5 * (sl[l] - sr[l]);
                sl[l] = m; sr[l] = s * alpha;
            }
        else
            for (int l = 0; l < len; l++)
            {
                faac_real m = 0.5 * (sl[l] - sr[l]);
                faac_real s = 0.5 * (sl[l] + sr[l]);
                sr[l] = m; sl[l] = s * alpha;
            }
        sl += BLOCK_LEN_SHORT;
        sr += BLOCK_LEN_SHORT;
    }
}

static inline void apply_is(faac_real * restrict sl, faac_real * restrict sr,
                            int start, int len, int wstart, int wend,
                            int in_phase, faac_real vfix)
{
    sl += wstart * BLOCK_LEN_SHORT + start;
    sr += wstart * BLOCK_LEN_SHORT + start;

    for (int win = wstart; win < wend; win++)
    {
        if (in_phase)
            for (int l = 0; l < len; l++) { sl[l] = (sl[l] + sr[l]) * vfix; sr[l] = 0.0; }
        else
            for (int l = 0; l < len; l++) { sl[l] = (sl[l] - sr[l]) * vfix; sr[l] = 0.0; }
        sl += BLOCK_LEN_SHORT;
        sr += BLOCK_LEN_SHORT;
    }
}

/* Content-adaptive M/S scaling: protects spatial anchors (low SFBs) and
 * monaural-dominant content (side energy < 2% total). */
static inline faac_real get_ms_alpha(int sfb, int sfmin, faac_real side_e,
                                     faac_real total_e, faac_real alpha,
                                     faac_real sidemin_q_en)
{
    if (total_e <= 0.0) return 0.0;
    faac_real side_ratio = side_e / total_e;
    if (sfb < sfmin + 2 || side_ratio < 0.10) return 1.0;
    if (side_ratio < sidemin_q_en) return 0.0;
    return alpha;
}

/* CPE joint processing: Handles IS, M/S, and Dual-Mono zeroing.
 * Consolidated to reduce code footprint and optimize cache locality. */
static int process_cpe(CoderInfo * restrict cl, CoderInfo * restrict cr,
                       ChannelInfo * restrict channel,
                       faac_real * restrict sl0, faac_real * restrict sr0,
                       int * restrict sfcnt, int wstart, int wend,
                       int mode, int is_start_sfb,
                       faac_real alpha, faac_real thrmid,
                       faac_real inv_isthr, faac_real thrside,
                       faac_real sidemin_q_en)
{
    int msused = 0;
    int sfmin = (cl->block_type == ONLY_SHORT_WINDOW) ? 1 : 8;
    const int * restrict sfb_offset = cl->sfb_offset;

    for (int sfb = 0; sfb < sfmin; sfb++) {
        if (channel) channel->msInfo.ms_used[(*sfcnt)] = 0;
        (*sfcnt)++;
    }

    for (int sfb = sfmin; sfb < cl->sfbn; sfb++)
    {
        int start = sfb_offset[sfb], len = sfb_offset[sfb+1] - start;
        faac_real enrgl = 0, enrgr = 0, enrglr = 0;
        const faac_real *lp = sl0 + wstart * BLOCK_LEN_SHORT + start;
        const faac_real *rp = sr0 + wstart * BLOCK_LEN_SHORT + start;

        for (int win = wstart; win < wend; win++) {
            for (int l = 0; l < len; l++) {
                enrgl += lp[l] * lp[l]; enrgr += rp[l] * rp[l]; enrglr += lp[l] * rp[l];
            }
            lp += BLOCK_LEN_SHORT; rp += BLOCK_LEN_SHORT;
        }

        faac_real efix = enrgl + enrgr;
        if (efix <= 0.0) {
            if (channel) channel->msInfo.ms_used[(*sfcnt)] = 0;
            (*sfcnt)++; continue;
        }

        int done = 0;
        if ((mode == JOINT_IS || (mode == JOINT_MIXED && sfb >= is_start_sfb)) && enrgl > 0.0 && enrgr > 0.0)
        {
            faac_real ethr = (FAAC_SQRT(enrgl) + FAAC_SQRT(enrgr));
            ethr = ethr * ethr * inv_isthr;
            faac_real enrgs_f = enrgl + enrgr + 2.0 * enrglr;
            faac_real enrgd_f = enrgl + enrgr - 2.0 * enrglr;
            int hcb = (enrgs_f >= ethr) ? HCB_INTENSITY : (enrgd_f >= ethr ? HCB_INTENSITY2 : HCB_NONE);

            if (hcb != HCB_NONE) {
                faac_real vfix = FAAC_SQRT(efix / (hcb == HCB_INTENSITY ? enrgs_f : enrgd_f));
                const faac_real step = 10/1.50515, inv_efix = 1.0 / efix;
                int sf = FAAC_LRINT(FAAC_LOG10(enrgl * inv_efix) * step);
                int pan = FAAC_LRINT(FAAC_LOG10(enrgr * inv_efix) * step) - sf;

                if (pan > 30) cl->book[*sfcnt] = HCB_ZERO;
                else if (pan < -30) cr->book[*sfcnt] = HCB_ZERO;
                else {
                    cl->sf[*sfcnt] = sf; cr->sf[*sfcnt] = -pan; cl->book[*sfcnt] = hcb;
                    apply_is(sl0, sr0, start, len, wstart, wend, hcb == HCB_INTENSITY, vfix);
                }
                if (channel) channel->msInfo.ms_used[*sfcnt] = 0;
                done = 1;
            }
        }

        if (!done && (mode == JOINT_MS || (mode == JOINT_MIXED && sfb < is_start_sfb)))
        {
            faac_real enrgs_m = (enrgl + enrgr + 2.0 * enrglr) * 0.25;
            faac_real enrgd_m = (enrgl + enrgr - 2.0 * enrglr) * 0.25;
            if ((min(enrgl, enrgr) * thrmid) >= max(enrgs_m, enrgd_m)) {
                int phase_in = (enrgs_m >= enrgd_m);
                faac_real a = get_ms_alpha(sfb, sfmin, (phase_in ? enrgd_m : enrgs_m), efix * 0.5, alpha, sidemin_q_en);
                apply_ms(sl0, sr0, start, len, wstart, wend, phase_in, a);
                if (channel) channel->msInfo.ms_used[*sfcnt] = 1;
                msused = 1; done = 1;
            }
        }

        if (!done && channel && (min(enrgl, enrgr) <= (thrside * max(enrgl, enrgr))))
        {
            if (enrgl < enrgr) zero_channel(sl0, start, len, wstart, wend);
            else zero_channel(sr0, start, len, wstart, wend);
        }
        if (channel && !done) channel->msInfo.ms_used[*sfcnt] = 0;
        (*sfcnt)++;
    }
    return msused;
}

void AACstereo(CoderInfo *coder, ChannelInfo *channel, faac_real *s[MAX_CHANNELS],
               int maxchan, faac_real quality, int mode, int sampleRate)
{
    static const faac_real sidemin = 0.05, sidemax = 0.2, thr075 = 1.09 - 1.0, thrmax = 1.25 - 1.0;
    faac_real alpha, thrmid, is_freq, inv_isthr, thrside, sidemin_q_en;

    /* Piecewise linear interpolation for quality-adaptive parameters.
     * Nodes derived via grid sweep to minimize IC error on speech/music
     * while preserving bit budget for monaural MOS (ViSQOL). */
    if (quality <= 0.5) {
        faac_real f = (max(0.37, quality) - 0.37) * (1.0 / 0.13);
        alpha = 0.01 + f * 0.02; is_freq = 5500.0;
    } else if (quality <= 1.0) {
        faac_real f = (quality - 0.5) * 2.0;
        alpha = 0.03 + f * 0.07; is_freq = 5500.0 + f * 2000.0;
    } else {
        faac_real f = (min(4.0, quality) - 1.0) * (1.0 / 3.0);
        alpha = 0.10 + f * 0.20; is_freq = 7500.0 + f * 2500.0;
    }
    if (is_freq > (sampleRate * 0.35)) is_freq = sampleRate * 0.35;

    thrmid = min((mode == JOINT_MIXED ? (thr075 * 0.85) : thr075) / quality, thrmax) + 1.0;
    thrmid *= thrmid;

    faac_real is_r = (mode == JOINT_IS) ? 0.18 / (quality * quality) : 0.18 / quality;
    faac_real is_tmp = min(is_r + 1.0, M_SQRT2);
    inv_isthr = 1.0 / (is_tmp * is_tmp);

    faac_real ts_tmp = min((sidemin * 1.6) / quality, sidemax * 1.6);
    thrside = ts_tmp * ts_tmp;
    faac_real sm_q = sidemin / quality;
    sidemin_q_en = sm_q * sm_q;

    for (int chn = 0; chn < maxchan; chn++) {
        if (!channel[chn].present || channel[chn].type != ELEMENT_CPE || !channel[chn].ch_is_left) continue;
        int rch = channel[chn].paired_ch;
        if (coder[chn].block_type != coder[rch].block_type || coder[chn].groups.n != coder[rch].groups.n) continue;

        channel[chn].common_window = 1;
        for (int cnt = 0; cnt < coder[chn].groups.n; cnt++)
            if (coder[chn].groups.len[cnt] != coder[rch].groups.len[cnt]) { channel[chn].common_window = 0; break; }
        if (!channel[chn].common_window) continue;

        channel[chn].msInfo.is_present = channel[rch].msInfo.is_present = (mode == JOINT_MS);

        int is_start_sfb = coder[chn].sfbn;
        if (mode == JOINT_MIXED) {
            int mdctlen = (coder[chn].block_type == ONLY_SHORT_WINDOW) ? 256 : 2048;
            int ifreq_bin = (int)((is_freq * (faac_real)mdctlen) / (faac_real)sampleRate);
            for (int sfb = 0; sfb < coder[chn].sfbn; sfb++)
                if (coder[chn].sfb_offset[sfb] >= ifreq_bin) { is_start_sfb = sfb; break; }
        }

        int sfcnt = 0, start = 0, msused = 0;
        for (int group = 0; group < coder[chn].groups.n; group++) {
            int end = start + coder[chn].groups.len[group];
            msused |= process_cpe(coder + chn, coder + rch, channel + chn, s[chn], s[rch], &sfcnt, start, end, mode, is_start_sfb, alpha, thrmid, inv_isthr, thrside, sidemin_q_en);
            start = end;
        }
        if (msused) channel[chn].msInfo.is_present = channel[rch].msInfo.is_present = 1;
    }
}
