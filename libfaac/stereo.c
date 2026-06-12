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
#define IS_STEP_CONST    (6.643856) /* 10/1.50515 */

enum {PH_NONE, PH_IN, PH_OUT};

static void stereo(CoderInfo * restrict cl, CoderInfo * restrict cr,
                   faac_real * restrict sl0, faac_real * restrict sr0, int * restrict sfcnt,
                   int wstart, int wend, faac_real phthr)
{
    if (!phthr) return;
    phthr = 1.0 / phthr;
    int sfmin = (cl->block_type == ONLY_SHORT_WINDOW) ? 1 : 8;
    const int * restrict sfb_off = cl->sfb_offset;
    (*sfcnt) += sfmin;
    for (int sfb = sfmin; sfb < cl->sfbn; sfb++) {
        int start = sfb_off[sfb], len = sfb_off[sfb + 1] - start;
        faac_real el = 0, er = 0, corr = 0;
        for (int w = wstart; w < wend; w++) {
            const faac_real * restrict sl = sl0 + w * 128 + start;
            const faac_real * restrict sr = sr0 + w * 128 + start;
            for (int l = 0; l < len; l++) {
                faac_real lx = sl[l], rx = sr[l];
                el += lx * lx; er += rx * rx; corr += lx * rx;
            }
        }
        faac_real efix = el + er;
        if (efix <= 0.0) { (*sfcnt)++; continue; }
        faac_real ethr = (FAAC_SQRT(el) + FAAC_SQRT(er));
        ethr *= ethr * phthr;
        faac_real es = el + er + 2.0 * corr;
        faac_real ed = el + er - 2.0 * corr;
        int hcb = HCB_NONE; faac_real vfix;
        if (es >= ethr) { hcb = HCB_INTENSITY; vfix = FAAC_SQRT(efix / es); }
        else if (ed >= ethr) { hcb = HCB_INTENSITY2; vfix = FAAC_SQRT(efix / ed); }
        if (hcb != HCB_NONE && el > 0.0 && er > 0.0) {
            int sf = FAAC_LRINT(FAAC_LOG10(el / efix) * IS_STEP_CONST);
            int pan = FAAC_LRINT(FAAC_LOG10(er / efix) * IS_STEP_CONST) - sf;
            if (pan > IS_PAN_LIMIT) { cl->book[*sfcnt] = HCB_ZERO; (*sfcnt)++; continue; }
            if (pan < -IS_PAN_LIMIT) { cr->book[*sfcnt] = HCB_ZERO; (*sfcnt)++; continue; }
            cl->sf[*sfcnt] = sf; cr->sf[*sfcnt] = -pan; cl->book[*sfcnt] = hcb;
            if (hcb == HCB_INTENSITY) {
                for (int w = wstart; w < wend; w++) {
                    faac_real * restrict sl = sl0 + w * 128 + start;
                    const faac_real * restrict sr = sr0 + w * 128 + start;
                    for (int l = 0; l < len; l++) sl[l] = (sl[l] + sr[l]) * vfix;
                }
            } else {
                for (int w = wstart; w < wend; w++) {
                    faac_real * restrict sl = sl0 + w * 128 + start;
                    const faac_real * restrict sr = sr0 + w * 128 + start;
                    for (int l = 0; l < len; l++) sl[l] = (sl[l] - sr[l]) * vfix;
                }
            }
        }
        (*sfcnt)++;
    }
}

static void midside(CoderInfo * restrict coder, ChannelInfo * restrict channel,
                    faac_real * restrict sl0, faac_real * restrict sr0, int * restrict sfcnt,
                    int wstart, int wend, faac_real thrmid, faac_real thrside)
{
    int sfmin = (coder->block_type == ONLY_SHORT_WINDOW) ? 1 : 8;
    const int * restrict sfb_off = coder->sfb_offset;
    for (int sfb = 0; sfb < sfmin; sfb++) channel->msInfo.ms_used[(*sfcnt)++] = 0;
    for (int sfb = sfmin; sfb < coder->sfbn; sfb++) {
        int start = sfb_off[sfb], len = sfb_off[sfb + 1] - start;
        faac_real el = 0, er = 0, corr = 0;
        for (int w = wstart; w < wend; w++) {
            const faac_real * restrict sl = sl0 + w * 128 + start;
            const faac_real * restrict sr = sr0 + w * 128 + start;
            for (int l = 0; l < len; l++) {
                faac_real lx = sl[l], rx = sr[l];
                el += lx * lx; er += rx * rx; corr += lx * rx;
            }
        }
        faac_real es = 0.25 * (el + er + 2.0 * corr);
        faac_real ed = 0.25 * (el + er - 2.0 * corr);
        int ms = 0;
        if ((min(el, er) * thrmid) >= max(es, ed)) {
            int ph = PH_NONE;
            if ((es * thrmid * 2.0) >= (el + er)) { ms = 1; ph = PH_IN; }
            else if ((ed * thrmid * 2.0) >= (el + er)) { ms = 1; ph = PH_OUT; }
            if (ms) {
                for (int w = wstart; w < wend; w++) {
                    faac_real * restrict sl = sl0 + w * 128 + start, * restrict sr = sr0 + w * 128 + start;
                    if (ph == PH_IN) for (int l = 0; l < len; l++) { faac_real m = 0.5*(sl[l]+sr[l]); sl[l]=m; sr[l]=0; }
                    else for (int l = 0; l < len; l++) { faac_real s = 0.5*(sl[l]-sr[l]); sr[l]=s; sl[l]=0; }
                }
            }
        }
        if (!ms && (min(el, er) <= (thrside * max(el, er)))) {
            if (el < er) for (int w = wstart; w < wend; w++) { faac_real * restrict sl = sl0 + w * 128 + start; for (int l = 0; l < len; l++) sl[l] = 0; }
            else for (int w = wstart; w < wend; w++) { faac_real * restrict sr = sr0 + w * 128 + start; for (int l = 0; l < len; l++) sr[l] = 0; }
        }
        channel->msInfo.ms_used[(*sfcnt)++] = ms;
    }
}

static int mixed(CoderInfo * restrict cl, CoderInfo * restrict cr, ChannelInfo * restrict channel,
                 faac_real * restrict sl0, faac_real * restrict sr0, int * restrict sfcnt,
                 int wstart, int wend, faac_real thrmid, faac_real thrside, faac_real isthr, int is_start_sfb)
{
    int sfmin = (cl->block_type == ONLY_SHORT_WINDOW) ? 1 : 8;
    const int * restrict sfb_off = cl->sfb_offset;
    for (int sfb = 0; sfb < sfmin; sfb++) channel->msInfo.ms_used[(*sfcnt)++] = 0;
    int msused = 0;
    for (int sfb = sfmin; sfb < cl->sfbn; sfb++) {
        int start = sfb_off[sfb], len = sfb_off[sfb + 1] - start;
        faac_real el = 0, er = 0, corr = 0;
        for (int w = wstart; w < wend; w++) {
            const faac_real * restrict sl = sl0 + w * 128 + start;
            const faac_real * restrict sr = sr0 + w * 128 + start;
            for (int l = 0; l < len; l++) {
                faac_real lx = sl[l], rx = sr[l];
                el += lx * lx; er += rx * rx; corr += lx * rx;
            }
        }
        faac_real efix = el + er;
        if (efix <= 0.0) { channel->msInfo.ms_used[(*sfcnt)++] = 0; continue; }
        faac_real es = el + er + 2.0 * corr;
        faac_real ed = el + er - 2.0 * corr;
        int ms = 0;
        if (sfb >= is_start_sfb && el > 0.0 && er > 0.0) {
            faac_real ethr = (FAAC_SQRT(el) + FAAC_SQRT(er)); ethr = (ethr * ethr) / isthr;
            int hcb = HCB_NONE; faac_real vfix;
            if (es >= ethr) { hcb = HCB_INTENSITY; vfix = FAAC_SQRT(efix / es); }
            else if (ed >= ethr) { hcb = HCB_INTENSITY2; vfix = FAAC_SQRT(efix / ed); }
            if (hcb != HCB_NONE) {
                int sf = FAAC_LRINT(FAAC_LOG10(el / efix) * IS_STEP_CONST);
                int pan = FAAC_LRINT(FAAC_LOG10(er / efix) * IS_STEP_CONST) - sf;
                if (pan > IS_PAN_LIMIT) { cl->book[*sfcnt] = HCB_ZERO; channel->msInfo.ms_used[(*sfcnt)++] = 0; continue; }
                if (pan < -IS_PAN_LIMIT) { cr->book[*sfcnt] = HCB_ZERO; channel->msInfo.ms_used[(*sfcnt)++] = 0; continue; }
                cl->sf[*sfcnt] = sf; cr->sf[*sfcnt] = -pan; cl->book[*sfcnt] = hcb;
                channel->msInfo.ms_used[(*sfcnt)++] = 0;
                for (int w = wstart; w < wend; w++) {
                    faac_real * restrict sl = sl0 + w * 128 + start, * restrict sr = sr0 + w * 128 + start;
                    if (hcb == HCB_INTENSITY) for (int l = 0; l < len; l++) { sl[l] = (sl[l] + sr[l]) * vfix; sr[l] = 0.0; }
                    else for (int l = 0; l < len; l++) { sl[l] = (sl[l] - sr[l]) * vfix; sr[l] = 0.0; }
                }
                continue;
            }
        }
        if ((min(el, er) * thrmid) >= max(es * 0.25, ed * 0.25)) {
            int ph = PH_NONE;
            if ((es * 0.25 * thrmid * 2.0) >= (el + er)) { ms = 1; ph = PH_IN; }
            else if ((ed * 0.25 * thrmid * 2.0) >= (el + er)) { ms = 1; ph = PH_OUT; }
            if (ms) {
                msused = 1;
                for (int w = wstart; w < wend; w++) {
                    faac_real * restrict sl = sl0 + w * 128 + start, * restrict sr = sr0 + w * 128 + start;
                    if (ph == PH_IN) for (int l = 0; l < len; l++) { faac_real m = 0.5*(sl[l]+sr[l]); sl[l] = m; sr[l] = 0.0; }
                    else for (int l = 0; l < len; l++) { faac_real s = 0.5*(sl[l]-sr[l]); sr[l] = s; sl[l] = 0.0; }
                }
            }
        }
        if (!ms && (min(el, er) <= (thrside * max(el, er)))) {
            if (el < er) for (int w = wstart; w < wend; w++) { faac_real * restrict sl = sl0 + w * 128 + start; for (int l = 0; l < len; l++) sl[l] = 0; }
            else for (int w = wstart; w < wend; w++) { faac_real * restrict sr = sr0 + w * 128 + start; for (int l = 0; l < len; l++) sr[l] = 0; }
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
        for (int g = 0; g < coder[chn].groups.n; g++)
            for (int b = 0; b < coder[chn].sfbn; b++) { coder[chn].book[bookcnt] = HCB_NONE; coder[chn].sf[bookcnt] = 0; bookcnt++; }
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
        for (int g = 0; g < coder[chn].groups.n; g++) {
            int end = start + coder[chn].groups.len[g];
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
