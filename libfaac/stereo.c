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
    along with this program.  See the GNU General Public License
    for more details.
****************************************************************************/

#define _USE_MATH_DEFINES

#include <math.h>
#include "stereo.h"
#include "huff2.h"

/* Psychoacoustic and bitstream constants */
#define IS_FREQ_LIMIT    6000
#define IS_PAN_LIMIT     30
#define IS_STEP_CONST    (6.643856189774724)

enum {PH_NONE, PH_IN, PH_OUT};

static void stereo(CoderInfo * __restrict cl, CoderInfo * __restrict cr,
                   faac_real * __restrict sl0, faac_real * __restrict sr0, int * __restrict sfcnt,
                   int wstart, int wend, faac_real phthr
                  )
{
    if (!phthr) return;
    phthr = 1.0 / phthr;
    int sfmin = (cl->block_type == ONLY_SHORT_WINDOW) ? 1 : 8;
    const int * __restrict sfb_offset = cl->sfb_offset;
    (*sfcnt) += sfmin;

    for (int sfb = sfmin; sfb < cl->sfbn; sfb++) {
        int start = sfb_offset[sfb], end = sfb_offset[sfb + 1], len = end - start;
        faac_real enrgs = 0, enrgd = 0, enrgl = 0, enrgr = 0;
        for (int win = wstart; win < wend; win++) {
            const faac_real * __restrict sl = sl0 + win * 128 + start;
            const faac_real * __restrict sr = sr0 + win * 128 + start;
            for (int l = 0; l < len; l++) {
                faac_real lx = sl[l], rx = sr[l];
                faac_real s = lx + rx, d = lx - rx;
                enrgs += s * s; enrgd += d * d;
                enrgl += lx * lx; enrgr += rx * rx;
            }
        }
        faac_real efix = enrgl + enrgr;
        if (efix <= 0.0) { (*sfcnt)++; continue; }
        faac_real ethr = (FAAC_SQRT(enrgl) + FAAC_SQRT(enrgr));
        ethr *= ethr * phthr;
        int hcb = HCB_NONE; faac_real vfix;
        if (enrgs >= ethr) { hcb = HCB_INTENSITY; vfix = FAAC_SQRT(efix / enrgs); }
        else if (enrgd >= ethr) { hcb = HCB_INTENSITY2; vfix = FAAC_SQRT(efix / enrgd); }

        if (hcb != HCB_NONE && enrgl > 0.0 && enrgr > 0.0) {
            int sf = FAAC_LRINT(FAAC_LOG10(enrgl / efix) * IS_STEP_CONST);
            int pan = FAAC_LRINT(FAAC_LOG10(enrgr / efix) * IS_STEP_CONST) - sf;
            if (pan > IS_PAN_LIMIT) { cl->book[*sfcnt] = HCB_ZERO; (*sfcnt)++; continue; }
            if (pan < -IS_PAN_LIMIT) { cr->book[*sfcnt] = HCB_ZERO; (*sfcnt)++; continue; }
            cl->sf[*sfcnt] = sf; cr->sf[*sfcnt] = -pan; cl->book[*sfcnt] = hcb;
            if (hcb == HCB_INTENSITY) {
                for (int win = wstart; win < wend; win++) {
                    faac_real * __restrict sl = sl0 + win * 128 + start;
                    const faac_real * __restrict sr = sr0 + win * 128 + start;
                    for (int l = 0; l < len; l++) sl[l] = (sl[l] + sr[l]) * vfix;
                }
            } else {
                for (int win = wstart; win < wend; win++) {
                    faac_real * __restrict sl = sl0 + win * 128 + start;
                    const faac_real * __restrict sr = sr0 + win * 128 + start;
                    for (int l = 0; l < len; l++) sl[l] = (sl[l] - sr[l]) * vfix;
                }
            }
        }
        (*sfcnt)++;
    }
}

static void midside(CoderInfo * __restrict coder, ChannelInfo * __restrict channel,
                    faac_real * __restrict sl0, faac_real * __restrict sr0, int * __restrict sfcnt,
                    int wstart, int wend, faac_real thrmid, faac_real thrside)
{
    int sfmin = (coder->block_type == ONLY_SHORT_WINDOW) ? 1 : 8;
    const int * __restrict sfb_offset = coder->sfb_offset;
    for (int sfb = 0; sfb < sfmin; sfb++) channel->msInfo.ms_used[(*sfcnt)++] = 0;
    for (int sfb = sfmin; sfb < coder->sfbn; sfb++) {
        int start = sfb_offset[sfb], end = sfb_offset[sfb + 1], len = end - start;
        faac_real enrgs = 0, enrgd = 0, enrgl = 0, enrgr = 0;
        for (int win = wstart; win < wend; win++) {
            const faac_real * __restrict sl = sl0 + win * 128 + start;
            const faac_real * __restrict sr = sr0 + win * 128 + start;
            for (int l = 0; l < len; l++) {
                faac_real lx = sl[l], rx = sr[l];
                faac_real s = 0.5 * (lx + rx), d = 0.5 * (lx - rx);
                enrgs += s * s; enrgd += d * d;
                enrgl += lx * lx; enrgr += rx * rx;
            }
        }
        int ms = 0;
        if ((min(enrgl, enrgr) * thrmid) >= max(enrgs, enrgd)) {
            int phase = PH_NONE;
            if ((enrgs * thrmid * 2.0) >= (enrgl + enrgr)) { ms = 1; phase = PH_IN; }
            else if ((enrgd * thrmid * 2.0) >= (enrgl + enrgr)) { ms = 1; phase = PH_OUT; }
            if (ms) {
                for (int win = wstart; win < wend; win++) {
                    faac_real * __restrict sl = sl0 + win * 128 + start, * __restrict sr = sr0 + win * 128 + start;
                    for (int l = 0; l < len; l++) {
                        faac_real lx = sl[l], rx = sr[l];
                        if (phase == PH_IN) { sl[l] = 0.5 * (lx + rx); sr[l] = 0.0; }
                        else { sl[l] = 0.0; sr[l] = 0.5 * (lx - rx); }
                    }
                }
            }
        }
        if (!ms && (min(enrgl, enrgr) <= (thrside * max(enrgl, enrgr)))) {
            if (enrgl < enrgr) for (int win = wstart; win < wend; win++) { faac_real * __restrict sl = sl0 + win * 128 + start; for (int l = 0; l < len; l++) sl[l] = 0.0; }
            else for (int win = wstart; win < wend; win++) { faac_real * __restrict sr = sr0 + win * 128 + start; for (int l = 0; l < len; l++) sr[l] = 0.0; }
        }
        channel->msInfo.ms_used[(*sfcnt)++] = ms;
    }
}

static int mixed(CoderInfo * __restrict cl, CoderInfo * __restrict cr, ChannelInfo * __restrict channel,
                 faac_real * __restrict sl0, faac_real * __restrict sr0, int * __restrict sfcnt,
                 int wstart, int wend, faac_real thrmid, faac_real thrside, faac_real isthr, int is_start_sfb)
{
    int sfmin = (cl->block_type == ONLY_SHORT_WINDOW) ? 1 : 8;
    const int * __restrict sfb_offset = cl->sfb_offset;
    for (int sfb = 0; sfb < sfmin; sfb++) channel->msInfo.ms_used[(*sfcnt)++] = 0;
    int msused = 0;
    for (int sfb = sfmin; sfb < cl->sfbn; sfb++) {
        int start = sfb_offset[sfb], end = sfb_offset[sfb + 1], len = end - start;
        faac_real enrgs = 0, enrgd = 0, enrgl = 0, enrgr = 0;
        for (int win = wstart; win < wend; win++) {
            const faac_real * __restrict sl = sl0 + win * 128 + start;
            const faac_real * __restrict sr = sr0 + win * 128 + start;
            for (int l = 0; l < len; l++) {
                faac_real lx = sl[l], rx = sr[l];
                faac_real sum = lx + rx, diff = lx - rx;
                enrgs += sum * sum; enrgd += diff * diff;
                enrgl += lx * lx; enrgr += rx * rx;
            }
        }
        faac_real efix = enrgl + enrgr;
        if (efix <= 0.0) { channel->msInfo.ms_used[(*sfcnt)++] = 0; continue; }
        int ms = 0;
        if (sfb >= is_start_sfb && enrgl > 0.0 && enrgr > 0.0) {
            faac_real ethr = (FAAC_SQRT(enrgl) + FAAC_SQRT(enrgr)); ethr = (ethr * ethr) / isthr;
            int hcb = HCB_NONE; faac_real vfix;
            if (enrgs >= ethr) { hcb = HCB_INTENSITY; vfix = FAAC_SQRT(efix / enrgs); }
            else if (enrgd >= ethr) { hcb = HCB_INTENSITY2; vfix = FAAC_SQRT(efix / enrgd); }
            if (hcb != HCB_NONE) {
                int sf = FAAC_LRINT(FAAC_LOG10(enrgl / efix) * IS_STEP_CONST);
                int pan = FAAC_LRINT(FAAC_LOG10(enrgr / efix) * IS_STEP_CONST) - sf;
                if (pan > 30) { cl->book[*sfcnt] = HCB_ZERO; channel->msInfo.ms_used[(*sfcnt)++] = 0; continue; }
                if (pan < -30) { cr->book[*sfcnt] = HCB_ZERO; channel->msInfo.ms_used[(*sfcnt)++] = 0; continue; }
                cl->sf[*sfcnt] = sf; cr->sf[*sfcnt] = -pan; cl->book[*sfcnt] = hcb;
                channel->msInfo.ms_used[(*sfcnt)++] = 0;
                for (int win = wstart; win < wend; win++) {
                    faac_real * __restrict sl = sl0 + win * 128 + start, * __restrict sr = sr0 + win * 128 + start;
                    for (int l = 0; l < len; l++) {
                        faac_real lx = sl[l], rx = sr[l];
                        if (hcb == HCB_INTENSITY) sl[l] = (lx + rx) * vfix; else sl[l] = (lx - rx) * vfix;
                        sr[l] = 0.0;
                    }
                }
                continue;
            }
        }
        if ((min(enrgl, enrgr) * thrmid) >= max(enrgs * 0.25, enrgd * 0.25)) {
            int phase = PH_NONE;
            if ((enrgs * 0.25 * thrmid * 2.0) >= (enrgl + enrgr)) { ms = 1; phase = PH_IN; }
            else if ((enrgd * 0.25 * thrmid * 2.0) >= (enrgl + enrgr)) { ms = 1; phase = PH_OUT; }
            if (ms) {
                msused = 1;
                for (int win = wstart; win < wend; win++) {
                    faac_real * __restrict sl = sl0 + win * 128 + start, * __restrict sr = sr0 + win * 128 + start;
                    for (int l = 0; l < len; l++) {
                        faac_real lx = sl[l], rx = sr[l];
                        if (phase == PH_IN) { sl[l] = 0.5 * (lx + rx); sr[l] = 0.0; }
                        else { sl[l] = 0.0; sr[l] = 0.5 * (lx - rx); }
                    }
                }
            }
        }
        if (!ms && (min(enrgl, enrgr) <= (thrside * max(enrgl, enrgr)))) {
            if (enrgl < enrgr) for (int win = wstart; win < wend; win++) { faac_real * __restrict sl = sl0 + win * 128 + start; for (int l = 0; l < len; l++) sl[l] = 0.0; }
            else for (int win = wstart; win < wend; win++) { faac_real * __restrict sr = sr0 + win * 128 + start; for (int l = 0; l < len; l++) sr[l] = 0.0; }
        }
        channel->msInfo.ms_used[(*sfcnt)++] = ms;
    }
    return msused;
}

void AACstereo(CoderInfo *coder, ChannelInfo *channel, faac_real *s[MAX_CHANNELS], int maxchan, faac_real quality, int mode, int sampleRate)
{
    static const faac_real thr075 = 1.09 - 1.0, thrmax = 1.25 - 1.0, sidemin = 0.1, sidemax = 0.3, isthrmax = M_SQRT2 - 1.0;
    faac_real thrmid = 1.0, thrside = 0.0, isthr = 1.0;
    switch (mode) {
    case JOINT_MIXED:
        thrmid = (thr075 * 0.5) / quality; if (thrmid > thrmax) thrmid = thrmax;
        thrside = sidemin / quality; if (thrside > sidemax) thrside = sidemax;
        thrmid += 1.0; isthr = 0.18 / (quality * quality) + 1.0; if (isthr > isthrmax + 1.0) isthr = isthrmax + 1.0; break;
    case JOINT_MS:
        thrmid = thr075 / quality; if (thrmid > thrmax) thrmid = thrmax;
        thrside = sidemin / quality; if (thrside > sidemax) thrside = sidemax;
        thrmid += 1.0; break;
    case JOINT_IS:
        isthr = 0.18 / (quality * quality) + 1.0; if (isthr > isthrmax + 1.0) isthr = isthrmax + 1.0; break;
    }
    thrmid *= thrmid; thrside *= thrside; isthr *= isthr;

    for (int chn = 0; chn < maxchan; chn++) {
        if (!channel[chn].present) continue;
        int bookcnt = 0;
        for (int group = 0; group < coder[chn].groups.n; group++)
            for (int band = 0; band < coder[chn].sfbn; band++) { coder[chn].book[bookcnt] = HCB_NONE; coder[chn].sf[bookcnt] = 0; bookcnt++; }
    }

    for (int chn = 0; chn < maxchan; chn++) {
        if (!channel[chn].present || !((channel[chn].type == ELEMENT_CPE) && (channel[chn].ch_is_left))) continue;
        int rch = channel[chn].paired_ch;
        channel[chn].common_window = 0; channel[chn].msInfo.is_present = 0; channel[rch].msInfo.is_present = 0;
        if (coder[chn].block_type != coder[rch].block_type || coder[chn].groups.n != coder[rch].groups.n) continue;
        channel[chn].common_window = 1;
        for (int cnt = 0; cnt < coder[chn].groups.n; cnt++) if (coder[chn].groups.len[cnt] != coder[rch].groups.len[cnt]) { channel[chn].common_window = 0; goto skip; }
        if (mode == JOINT_MS) { channel[chn].msInfo.is_present = 1; channel[rch].msInfo.is_present = 1; }
        int is_start_sfb = coder[chn].sfbn;
        if (mode == JOINT_MIXED) {
            int mdctlen = (coder[chn].block_type == ONLY_SHORT_WINDOW) ? 256 : 2048;
            for (int sfb = 0; sfb < coder[chn].sfbn; sfb++) if ((coder[chn].sfb_offset[sfb] * sampleRate) / mdctlen >= 6000) { is_start_sfb = sfb; break; }
        }
        int sfcnt = 0, start = 0, msused = 0;
        for (int group = 0; group < coder[chn].groups.n; group++) {
            int end = start + coder[chn].groups.len[group];
            switch(mode) {
            case JOINT_MS: midside(coder+chn, channel+chn, s[chn], s[rch], &sfcnt, start, end, thrmid, thrside); break;
            case JOINT_IS: stereo(coder+chn, coder+rch, s[chn], s[rch], &sfcnt, start, end, isthr); break;
            case JOINT_MIXED: msused |= mixed(coder+chn, coder+rch, channel+chn, s[chn], s[rch], &sfcnt, start, end, thrmid, thrside, isthr, is_start_sfb); break;
            default: sfcnt += coder[chn].sfbn; break;
            }
            start = end;
        }
        if ((mode == JOINT_MIXED) && msused) { channel[chn].msInfo.is_present = 1; channel[rch].msInfo.is_present = 1; }
        skip:;
    }
}
