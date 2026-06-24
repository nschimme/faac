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
#include <stdlib.h>
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

/* Mono collapse: nearly all the band energy is in one of mid/side, so keep
 * that one (mid for in-phase, side for anti-phase) and drop the other. Lossy
 * but cheap; only chosen when one of mid/side dominates by coll_thr. */
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
            for (l = 0; l < len; l++)
            {
                sl_out[l] = 0.5 * (sl_out[l] + sr_out[l]);
                sr_out[l] = 0.0;
            }
        else
            for (l = 0; l < len; l++)
            {
                sr_out[l] = 0.5 * (sl_out[l] - sr_out[l]);
                sl_out[l] = 0.0;
            }
        sl_out += BLOCK_LEN_SHORT;
        sr_out += BLOCK_LEN_SHORT;
    }
}

/* True (lossless) M/S: store mid=(L+R)/2 in the left channel and
 * side=(L-R)/2 in the right. The decoder reconstructs L=M+S, R=M-S exactly,
 * preserving the full stereo image. */
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

/* Collapse the band to the left (intensity) channel and zero the right: the
 * intensity codebook + level ratio carry the band, so the right spectrum is
 * never coded.  Always zeroing it keeps any stale side energy out. */
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
            for (l = 0; l < len; l++) { sl_out[l] = (sl_out[l] + sr_out[l]) * vfix; sr_out[l] = 0.0; }
        else
            for (l = 0; l < len; l++) { sl_out[l] = (sl_out[l] - sr_out[l]) * vfix; sr_out[l] = 0.0; }
        sl_out += BLOCK_LEN_SHORT;
        sr_out += BLOCK_LEN_SHORT;
    }
}


/* Intensity Stereo: send one combined channel + per-band L/R level ratio;
 * the decoder re-pans it. Discards inter-channel phase, so it is gated by
 * phthr and skipped on low bands where phase still matters. */
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

    /* low bands skip IS: phase still audible there */
    if (cl->block_type == ONLY_SHORT_WINDOW)
        sfmin = 1;
    else
        sfmin = 8;

    *sfcnt += sfmin;

    for (sfb = sfmin; sfb < cl->sfbn; sfb++)
    {
        int win;
        int start = sfb_offset[sfb];
        int end = sfb_offset[sfb + 1];
        int len = end - start;
        faac_real enrgs, enrgd, enrgl, enrgr, enrglr;
        int hcb = HCB_NONE;
        const faac_real step = 10/1.50515;
        faac_real ethr;
        faac_real vfix, efix;
        const faac_real * restrict sl_ptr = sl0 + wstart * BLOCK_LEN_SHORT + start;
        const faac_real * restrict sr_ptr = sr0 + wstart * BLOCK_LEN_SHORT + start;

        /* one pass: accumulate L/R energies and the L*R cross term, then
         * derive sum/diff energies via |l+-r|^2 = l^2 + r^2 +- 2*l*r */
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
        /* float cancellation for L~=+-R can round these below zero; clamp before FAAC_SQRT (NaN guard) */
        if (enrgs < 0.0) enrgs = 0.0;
        if (enrgd < 0.0) enrgd = 0.0;

        /* Skip completely silent bands first: efix==0 makes ethr==0 so IS
         * would trigger spuriously, and vfix=sqrt(0/0) would be NaN. Doing
         * this before ethr also skips its two FAAC_SQRT on empty bands. */
        efix = enrgl + enrgr;
        if (efix <= 0.0)
        {
            (*sfcnt)++;
            continue;
        }

        /* IS pays off when one phase collapses most energy into a single
         * channel: gate on (sqrt(L)+sqrt(R))^2 scaled by phthr */
        ethr = FAAC_SQRT(enrgl) + FAAC_SQRT(enrgr);
        ethr *= ethr;
        ethr *= phthr;
        /* in-phase (l+r) vs out-of-phase (l-r); vfix renormalises the kept
         * channel so its energy matches the original L+R total */
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
            /* pan = L/R level ratio in scalefactor steps (~1.5 dB each),
             * the intensity position the decoder uses to re-spread the band */
            faac_real inv_efix = 1.0 / efix;
            int sf = FAAC_LRINT(FAAC_LOG10(enrgl * inv_efix) * step);
            int pan = FAAC_LRINT(FAAC_LOG10(enrgr * inv_efix) * step) - sf;

            /* pan beyond +-30 steps: the quieter channel is inaudible,
             * so drop it entirely (HCB_ZERO) instead of IS-coding */
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

            /* the intensity codebook marks the band, so the right channel
             * spectrum is not coded; apply_is zeroes it to be safe. */
            apply_is(sl0, sr0, start, len, wstart, wend,
                     hcb == HCB_INTENSITY, vfix);
        }
        (*sfcnt)++;
    }
}

/* Mid/Side: code mid=(L+R)/2 and side=(L-R)/2 instead of L/R. Lossless when
 * both are kept (apply_ms_true); when one of mid/side dominates by coll_thr
 * the band is collapsed to that one (apply_ms_mono) to save bits. thrside
 * additionally zeroes a channel that is far quieter. */
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
        faac_real enrgs, enrgd, enrgl, enrgr, enrglr;
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
            /* mid/side imbalanced beyond coll_thr: collapse to the dominant
             * one (lossy, saves bits); otherwise keep both via true M/S */
            if (enrgs > (enrgd * coll_thr))
                apply_ms_mono(sl0, sr0, start, len, wstart, wend, 1);
            else if (enrgd > (enrgs * coll_thr))
                apply_ms_mono(sl0, sr0, start, len, wstart, wend, 0);
            else
                apply_ms_true(sl0, sr0, start, len, wstart, wend);
        }

        /* one channel far quieter than the other: zero it (the louder one
         * masks the loss). Skip if just M/S-coded: sl/sr hold mid/side now,
         * so zeroing one would silence the band on decode. */
        if (!ms && (min(enrgl, enrgr) <= (thrside * max(enrgl, enrgr))))
        {
            if (enrgl < enrgr)
                zero_channel(sl0, start, len, wstart, wend);
            else
                zero_channel(sr0, start, len, wstart, wend);
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
                 faac_real thrmid, faac_real thrside, faac_real inv_isthr,
                 int is_start_sfb, faac_real coll_thr
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
        faac_real enrgs, enrgd, enrgl, enrgr, enrglr;
        faac_real efix;
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
        /* float cancellation for L~=+-R can round these below zero; clamp before FAAC_SQRT (NaN guard) */
        if (enrgs < 0.0) enrgs = 0.0;
        if (enrgd < 0.0) enrgd = 0.0;

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
            ethr *= inv_isthr;

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
                faac_real inv_efix = 1.0 / efix;
                int sf = FAAC_LRINT(FAAC_LOG10(enrgl * inv_efix) * step);
                int pan = FAAC_LRINT(FAAC_LOG10(enrgr * inv_efix) * step) - sf;

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
                /* signaling intensity stereo by right channel codebook */
                cr->book[*sfcnt] = hcb;
                channel->msInfo.ms_used[(*sfcnt)++] = 0;

                /* drop the right channel: the codebook + intensity position
                 * carry the band, so its spectrum is not coded */
                apply_is(sl0, sr0, start, len, wstart, wend,
                         hcb == HCB_INTENSITY, vfix);
                continue;
            }
        }

        /* M/S decision: enrgs/enrgd are computed without the 0.5 mid/side
         * factor of midside(), hence the 0.25 energy compensation. */
        if ((min(enrgl, enrgr) * thrmid) >= max(enrgs * 0.25, enrgd * 0.25))
        {
            ms = 1;
            msused = 1;
            /* mid/side imbalanced beyond coll_thr: collapse to the dominant
             * one (lossy, saves bits); otherwise keep both via true M/S */
            if (enrgs > (enrgd * coll_thr))
                apply_ms_mono(sl0, sr0, start, len, wstart, wend, 1);
            else if (enrgd > (enrgs * coll_thr))
                apply_ms_mono(sl0, sr0, start, len, wstart, wend, 0);
            else
                apply_ms_true(sl0, sr0, start, len, wstart, wend);
        }

        if (!ms && (min(enrgl, enrgr) <= (thrside * max(enrgl, enrgr))))
        {
            if (enrgl < enrgr)
                zero_channel(sl0, start, len, wstart, wend);
            else
                zero_channel(sr0, start, len, wstart, wend);
        }

        channel->msInfo.ms_used[(*sfcnt)++] = ms;
    }

    return msused;
}

static inline faac_real env_real(const char *name, faac_real def)
{
    const char *e = getenv(name);
    return e ? (faac_real)atof(e) : def;
}

static inline int env_int(const char *name, int def)
{
    const char *e = getenv(name);
    return e ? atoi(e) : def;
}

void StereoTuningInit(StereoTuning *tune)
{
    /* Defaults chosen via MOS-gated benchmark sweeps to balance stereo image
     * coherence (ic_err) and monaural fidelity (MOS). Conservative values
     * (coll_thr_mid=1.5, is_freq_lo=5500) are used at 128k to avoid
     * monaural regressions. */
    tune->coll_thr_lo = env_real("FAAC_COLL_THR_LO", 1.0);
    tune->coll_thr_mid = env_real("FAAC_COLL_THR_MID", 1.5);
    tune->coll_thr_hi = env_real("FAAC_COLL_THR_HI", 60.0);
    tune->coll_thr_scale = env_real("FAAC_COLL_THR_SCALE", 1.0);
    tune->is_freq_lo = env_int("FAAC_IS_FREQ_LO", 5500);
    tune->is_freq_hi = env_int("FAAC_IS_FREQ_HI", 10000);
    tune->thrside_scale = env_real("FAAC_THRSIDE_SCALE", 1.6);
}

void AACstereo(CoderInfo *coder,
               ChannelInfo *channel,
               faac_real *s[MAX_CHANNELS],
               int maxchan,
               faac_real quality,
               int mode,
               int sampleRate,
               const StereoTuning *tune
              )
{
    int chn;
    static const faac_real thr075 = 1.09 /* ~0.75dB */ - 1.0;
    static const faac_real thrmax = 1.25 /* ~2dB */ - 1.0;
    static const faac_real sidemin_base = 0.05; /* -26dB */
    static const faac_real sidemax_base = 0.2; /* -14dB */
    static const faac_real isthrmax = M_SQRT2 - 1.0;
    faac_real thrmid, thrside;
    faac_real isthr;
    faac_real coll_thr;
    int is_freq;
    faac_real sidemin = sidemin_base * tune->thrside_scale;
    faac_real sidemax = sidemax_base * tune->thrside_scale;

    /* Quality-adaptive hybrid gate. coll_thr is the mid/side dominance ratio
     * above which a band is collapsed to one channel (apply_ms_mono) rather
     * than kept as true M/S; is_freq is the intensity-stereo floor frequency.
     * Low quality -> aggressive bit-saving; high quality -> spatial fidelity.
     * Piecewise-linear and continuous at the 0.5/1.0/4.0 breakpoints. */
    if (quality <= 0.5)
    {
        coll_thr = tune->coll_thr_lo;
        is_freq = tune->is_freq_lo;
    }
    else
    {
        /* is_freq interpolation: linear between 0.5 and 4.0 */
        faac_real f_is = min(1.0, (quality - 0.5) / 3.5);
        is_freq = tune->is_freq_lo + FAAC_LRINT((faac_real)(tune->is_freq_hi - tune->is_freq_lo) * f_is);

        if (quality < 1.0)
        {
            faac_real f = (quality - 0.5) * 2.0; /* 0..1 */
            coll_thr = tune->coll_thr_lo + (tune->coll_thr_mid - tune->coll_thr_lo) * f;
        }
        else if (quality < 4.0)
        {
            faac_real f = (quality - 1.0) / 3.0; /* 0..1 */
            coll_thr = tune->coll_thr_mid + (tune->coll_thr_hi - tune->coll_thr_mid) * f;
        }
        else
        {
            coll_thr = tune->coll_thr_hi;
            is_freq = tune->is_freq_hi;
        }
    }
    coll_thr *= tune->coll_thr_scale;

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

        /* find the first SFB at/above the quality-adaptive IS floor; mixed()
         * does IS from here up, M/S below, where phase still carries the
         * stereo image */
        if (mode == JOINT_MIXED)
        {
            int sfb;
            int mdctlen = (coder[chn].block_type == ONLY_SHORT_WINDOW)
                          ? (2 * BLOCK_LEN_SHORT) : (2 * BLOCK_LEN_LONG);
            /* cap the IS floor at 70% of Nyquist: at low sample rates the
             * floor can exceed the top band and disable IS for the whole frame */
            int floor_freq = is_freq;
            int cap = (sampleRate * 7) / 20;
            if (floor_freq > cap)
                floor_freq = cap;

            is_start_sfb = coder[chn].sfbn;
            for (sfb = 0; sfb < coder[chn].sfbn; sfb++)
            {
                /* bin center -> Hz: offset * fs / mdctlen */
                int freq = (coder[chn].sfb_offset[sfb] * sampleRate) / mdctlen;
                if (freq >= floor_freq)
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
                        start, end, thrmid, thrside, coll_thr);
                break;
            case JOINT_IS:
                stereo(coder + chn, coder + rch, s[chn], s[rch], &sfcnt, start, end, isthr);
                break;
            case JOINT_MIXED:
                msused |= mixed(coder + chn, coder + rch, channel + chn,
                                s[chn], s[rch], &sfcnt, start, end,
                                thrmid, thrside, 1.0 / isthr, is_start_sfb, coll_thr);
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
