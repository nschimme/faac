/* Probe-only. See step1.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "step1.h"
#include "frame.h"
#include "huff2.h"
#include "tns.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define STEP1_MAX_FRAMES 8192

struct Step1Ctx {
    int offset;
    ReemitICS (*f)[2]; /* [frame][ch] */
    int max_frame;
};

struct Step1Ctx *Step1Get(void)
{
    static struct Step1Ctx *ctx;
    static int tried;
    if (tried) return ctx;
    tried = 1;

    const char *path = getenv("FAAC_STEP1");
    if (!path || !*path) return NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    struct Step1Ctx *s1 = calloc(1, sizeof(*s1));
    s1->f = calloc(STEP1_MAX_FRAMES, sizeof(*s1->f));
    const char *offs = getenv("FAAC_STEP1_OFFSET");
    s1->offset = offs ? (int)strtol(offs, NULL, 0) : 0;

    int n = 0;
    while (n < STEP1_MAX_FRAMES) {
        int c = fgetc(f);
        if (c == EOF) break;
        ungetc(c, f);
        ReemitReadICS(f, &s1->f[n][0]);
        ReemitReadICS(f, &s1->f[n][1]);
        n++;
    }
    fclose(f);
    s1->max_frame = n;
    ctx = s1;
    return ctx;
}

int Step1Lookup(struct Step1Ctx *s1, int ciFrame, int ch, ReemitICS *out)
{
    if (!s1) return 0;
    int n = ciFrame + s1->offset;
    if (n < 0 || n >= s1->max_frame || ch < 0 || ch > 1) return 0;
    if (!s1->f[n][ch].present) return 0;
    *out = s1->f[n][ch];
    return 1;
}

/* ISO/IEC 14496-3 Table 4.139, TNS_MAX_BANDS by sampling_frequency_index --
 * this project's own libfaad/tns.c already carries the same clean-room copy
 * of this spec table (no reference-decoder source involved either place). */
static const uint8_t tns_max_bands_long[12]  = { 31, 31, 34, 40, 42, 51, 46, 46, 42, 42, 42, 39 };
static const uint8_t tns_max_bands_short[12] = {  9,  9, 10, 14, 14, 14, 14, 14, 14, 14, 14, 14 };

static int tns_max_bands_for(int sr_idx, int is_short)
{
    if (sr_idx < 0 || sr_idx > 11) sr_idx = 4;
    return is_short ? tns_max_bands_short[sr_idx] : tns_max_bands_long[sr_idx];
}

/* Forward (analysis) counterpart of libfaad/tns.c's tns_ar_filter: same
 * region traversal and direction handling, PLUS instead of MINUS (FAAC's
 * finalize_filter/filter_spec convention, already verified to invert
 * correctly against a standard AR-synthesis decoder -- see TnsEncode's own
 * use of it). `a[]` is 1-indexed (a[0]=1 implicit, matches TnsFilterData). */
static void step1_ar_filter(float *spec, int length, int dir, const float *a, int order)
{
    float tmp[FRAME_LEN];
    int i, j;
    if (length <= 0 || order <= 0 || length > FRAME_LEN) return;

    if (!dir) {
        for (i = 0; i < length; i++) {
            float acc = spec[i];
            int limit = i < order ? i : order;
            for (j = 1; j <= limit; j++)
                acc += a[j] * spec[i - j];
            tmp[i] = acc;
        }
    } else {
        for (i = length - 1; i >= 0; i--) {
            float acc = spec[i];
            int limit = (length - 1 - i) < order ? (length - 1 - i) : order;
            for (j = 1; j <= limit; j++)
                acc += a[j] * spec[i + j];
            tmp[i] = acc;
        }
    }
    memcpy(spec, tmp, length * sizeof(float));
}

void Step1ApplyTns(CoderInfo *ci, float *spec, const ReemitICS *rec, int sr_idx)
{
    int is_short = (rec->win_seq == ONLY_SHORT_WINDOW);
    int nwin = is_short ? MAX_SHORT_WINDOWS : 1;
    int max_order = is_short ? 7 : 12;
    int tns_max_bands = tns_max_bands_for(sr_idx, is_short);
    /* Full sfb-table width for this block type, matching FAAD3's apply_tns
     * ics->num_sfbs (NOT rec->max_sfb, the coded band count) -- filters
     * stack down from here, per ISO 14496-3 4.6.9. */
    int full_sfb_count = is_short ? ci->tnsInfo.tnsNumSwbShort : ci->tnsInfo.tnsNumSwbLong;

    if (!rec->tns_present) return;

    for (int w = 0; w < nwin; w++) {
        int nfilt = rec->tns_num_filt[w];
        if (nfilt <= 0) continue;
        int coef_res = rec->tns_coef_res[w] + DEF_TNS_RES_OFFSET;
        int bottom = full_sfb_count;
        int top;
        float *window_spec = is_short ? spec + w * BLOCK_LEN_SHORT : spec;
        int limit = rec->max_sfb < tns_max_bands ? rec->max_sfb : tns_max_bands;

        for (int fnum = 0; fnum < nfilt && fnum < TNS_MAX_FILTERS; fnum++) {
            int order = rec->tns_order[w][fnum];
            int length = rec->tns_length[w][fnum];
            int dir = rec->tns_direction[w][fnum];
            top = bottom;
            bottom = (top > length) ? (top - length) : 0;
            if (order <= 0) continue;
            if (order > max_order) order = max_order;

            int b0 = bottom < limit ? bottom : limit;
            int b1 = top < limit ? top : limit;
            int start_line = ci->sfb_offset[b0];
            int end_line = ci->sfb_offset[b1];
            int num_lines = end_line - start_line;
            if (num_lines <= 0) continue;

            float k[TNS_MAX_ORDER + 1] = {0};
            float a[TNS_MAX_ORDER + 1] = {0};
            float half = (coef_res == 4) ? 8.0f : 4.0f;
            float s_p = (half - 0.5f) / (float)(M_PI / 2.0);
            float s_n = (half + 0.5f) / (float)(M_PI / 2.0);
            for (int i = 1; i <= order; i++) {
                int val = rec->tns_coef[w][fnum][i - 1];
                float s = (val >= 0) ? s_p : s_n;
                k[i] = sinf((float)val / s);
            }
            TnsFinalizeFilterProbe(order, k, a);
            step1_ar_filter(window_spec + start_line, num_lines, dir, a, order);
        }
    }
}

void Step1ApplyMS(const ReemitICS *left, const ReemitICS *right,
                   const int *sfb_offset, float *specL, float *specR)
{
    int absw = 0;
    for (int g = 0; g < left->num_groups; g++) {
        int glen = left->group_len[g];
        for (int sfb = 0; sfb < left->max_sfb; sfb++) {
            int band = g * left->max_sfb + sfb;
            if (!left->band[band].ms) continue;
            int lcb = left->band[band].cb, rcb = right->band[band].cb;
            int l_reg = (lcb >= HCB_1 && lcb <= HCB_ESC);
            int r_reg = (rcb >= HCB_1 && rcb <= HCB_ESC);
            /* libfaad/stereo.c's apply_ms_stereo: PNS-vs-PNS is handled as
             * correlated noise (no real spectrum involved either side), and
             * a mismatched PNS/regular pair skips the transform entirely.
             * Only apply the real butterfly when both sides are genuinely
             * coded spectral data. */
            if (!(l_reg && r_reg)) continue;
            int lo = sfb_offset[sfb], hi = sfb_offset[sfb + 1];
            for (int w = 0; w < glen; w++) {
                int base = (left->win_seq == ONLY_SHORT_WINDOW) ? (absw + w) * 128 : 0;
                for (int k = lo; k < hi; k++) {
                    float l = specL[base + k], r = specR[base + k];
                    specL[base + k] = 0.5f * (l + r);
                    specR[base + k] = 0.5f * (l - r);
                }
            }
        }
        absw += glen;
    }
}

/* Mirrors quantize.c's sfac_to_gain/gain_with_overflow_clamp exactly (values
 * copied from quantize.h/quantize.c's own constants, not re-derived) so a
 * band that would escape-overflow FAAC's Huffman tables is clamped the same
 * way a normal encode clamps it, rather than producing an invalid escape
 * code. */
#define STEP1_SF_STEP 13.287712379549461f
#define STEP1_SF_OFFSET 100

/* Probe-only, ladder rounding-variant sweep: FAAC's own MAGIC_NUMBER
 * (quantize.h) is 0.4054 (a rounding-offset chosen for FAAC's own encode,
 * not necessarily optimal for step1's different task -- matching a
 * reference's already-chosen scale rather than searching for FAAC's own).
 * FAAC_STEP1_MAGIC overrides it for this sweep; unset behaves exactly as
 * before (0.4054f). */
static float step1_magic(void)
{
    static float m = -1.0f;
    if (m < 0.0f) {
        const char *e = getenv("FAAC_STEP1_MAGIC");
        m = e ? (float)atof(e) : 0.4054f;
    }
    return m;
}

static float step1_gain(int sf_abs, float band_peak)
{
    int sfac = STEP1_SF_OFFSET - sf_abs;
    float gain = powf(10.0f, (float)sfac / STEP1_SF_STEP);
    float magic = step1_magic();
    float max_quant_limit = powf((float)MAX_HUFF_ESC_VAL + 1.0f - magic, 4.0f / 3.0f);
    if (band_peak > 0.0f && gain * band_peak > max_quant_limit)
        gain = max_quant_limit / band_peak;
    return gain;
}

static int step1_quantize_line(float val, float gain)
{
    float tmp = fabsf(val * gain);
    tmp = sqrtf(tmp * sqrtf(tmp));
    int q = (int)(tmp + step1_magic());
    return (val < 0.0f) ? -q : q;
}

void Step1Quantize(CoderInfo *ci, float *spec, const ReemitICS *rec, int *zero_count,
                    float scale)
{
    int qs[FRAME_LEN];
    int qlen = 0;
    int absw = 0;

    ci->bandcnt = rec->num_bands;
    ci->datacnt = 0;
    ci->global_gain = rec->global_gain;

    for (int g = 0; g < rec->num_groups; g++) {
        int glen = rec->group_len[g];
        for (int sfb = 0; sfb < rec->max_sfb; sfb++) {
            int band = g * rec->max_sfb + sfb;
            int cb = rec->band[band].cb;
            int sf = rec->band[band].sf;
            ci->sf[band] = sf;

            if (cb < HCB_1 || cb > HCB_ESC) {
                ci->book[band] = cb; /* 0, 13, 14 or 15: pass through, no coefficient data */
                continue;
            }

            /* Matches quantize.c's measure_band_energy exactly: the MEAN of
             * each window's own peak (squared then rooted), not a single max
             * over every line in the group -- these differ once glen>1, and
             * the overflow clamp in step1_gain reads band_peak, so a
             * mismatched peak can silently shift the gain on a loud band. */
            int lo = ci->sfb_offset[sfb], hi = ci->sfb_offset[sfb + 1];
            float peak_energy_sum = 0.0f;
            for (int w = 0; w < glen; w++) {
                int base = (rec->win_seq == ONLY_SHORT_WINDOW) ? (absw + w) * 128 : 0;
                float wpeak = 0.0f;
                for (int k = lo; k < hi; k++) {
                    float v = spec[base + k];
                    float e = v * v;
                    if (e > wpeak) wpeak = e;
                }
                peak_energy_sum += wpeak;
            }
            float band_peak = sqrtf(peak_energy_sum / (float)glen);
            /* `scale` is a probe-only, uniform last-resort fallback (see
             * frame.c's retry loop): without a rate loop, forcing the
             * reference's exact absolute scalefactors onto FAAC's own
             * spectrum can legitimately ask for more bits than ISO 14496-3
             * §6.4's 6144-bits/channel ceiling allows (observed on velvet:
             * a heavily-M/S, escape-heavy long block). Scaling every band's
             * gain down uniformly and retrying trades measurement fidelity
             * for a valid bitstream on the rare frame that doesn't fit --
             * the alternative is dropping the frame, which is worse for
             * this ladder's whole-clip decode-based scoring. */
            float gain = step1_gain(sf, band_peak) * scale;

            int band_start = qlen;
            int maxq = 0;
            for (int w = 0; w < glen; w++) {
                int base = (rec->win_seq == ONLY_SHORT_WINDOW) ? (absw + w) * 128 : 0;
                for (int k = lo; k < hi; k++) {
                    int q = step1_quantize_line(spec[base + k], gain);
                    int a = q < 0 ? -q : q;
                    if (a > maxq) maxq = a;
                    qs[qlen++] = q;
                }
            }
            if (!maxq) {
                /* A regular band that quantizes to all zeros MUST be coded
                 * HCB_ZERO -- an all-zero cb 1-11 section is not a valid
                 * bitstream (real encoders enforce this too). */
                ci->book[band] = HCB_ZERO;
                qlen = band_start; /* the huffman-coding pass below never sees these lines */
                if (zero_count) (*zero_count)++;
            } else {
                /* Bug found via cross-reference desync (not caught by the
                 * self-test, where FAAC's own quantized magnitudes always
                 * fit the class FAAC itself picked for them): `cb` is the
                 * REFERENCE's class for ITS OWN magnitudes at this band, not
                 * necessarily big enough for what FAAC's real spectrum
                 * quantizes to at the reference's scale. huffbook()'s
                 * Viterbi only WIDENS from whatever book[] arrives with
                 * (`lo = ((book-1)&~1)+1`); if `cb` under-covers maxq, the
                 * too-small family's cost tables (size_books' HCB_1/3/5
                 * index math) get indexed with an out-of-range magnitude --
                 * an out-of-bounds read that can make the invalid book look
                 * cheapest, so huffbook picks a book that can't represent
                 * these values and the writer emits fewer bits than the
                 * decoder expects (a real decoder desync, not just a
                 * quality difference). Recompute the minimum sufficient
                 * class from the ACTUAL maxq, exactly like BlocQuant's own
                 * assign_band_codebooks does, and let huffbook widen from
                 * there -- never narrow below what these values need. */
                ci->book[band] = maxq <= LAV_1 ? HCB_1 : maxq <= LAV_2 ? HCB_3
                               : maxq <= LAV_4 ? HCB_5 : maxq <= LAV_7 ? HCB_7
                               : maxq <= LAV_12 ? HCB_9 : HCB_ESC;
            }
        }
        absw += glen;
    }

    huffbook(ci, qs);
}
