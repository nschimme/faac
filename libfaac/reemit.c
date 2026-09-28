/* Probe-only. See reemit.h. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "reemit.h"
#include "frame.h"
#include "channels.h"
#include "huff2.h"
#include "bitstream.h"

static int32_t reemit_read_i32(FILE *f)
{
    int32_t v;
    if (fread(&v, sizeof v, 1, f) != 1) {
        fprintf(stderr, "reemit: unexpected EOF reading intermediate\n");
        exit(1);
    }
    return v;
}

void ReemitReadICS(FILE *f, ReemitICS *r)
{
    int i, w, ff;
    memset(r, 0, sizeof(*r));
    r->present = reemit_read_i32(f);
    r->win_seq = reemit_read_i32(f);
    r->window_shape = reemit_read_i32(f);
    r->max_sfb = reemit_read_i32(f);
    r->num_groups = reemit_read_i32(f);
    for (i = 0; i < REEMIT_MAX_WINDOWS; i++) r->group_len[i] = reemit_read_i32(f);
    r->global_gain = reemit_read_i32(f);
    r->num_bands = reemit_read_i32(f);
    for (i = 0; i < MAX_SCFAC_BANDS; i++) r->band[i].cb = reemit_read_i32(f);
    for (i = 0; i < MAX_SCFAC_BANDS; i++) r->band[i].sf = reemit_read_i32(f);
    for (i = 0; i < MAX_SCFAC_BANDS; i++) r->band[i].ms = reemit_read_i32(f);
    for (i = 0; i < FRAME_LEN; i++) r->quantized[i] = reemit_read_i32(f);
    r->tns_present = reemit_read_i32(f);
    for (w = 0; w < REEMIT_MAX_WINDOWS; w++) r->tns_num_filt[w] = reemit_read_i32(f);
    for (w = 0; w < REEMIT_MAX_WINDOWS; w++) r->tns_coef_res[w] = reemit_read_i32(f);
    for (w = 0; w < REEMIT_MAX_WINDOWS; w++)
        for (ff = 0; ff < TNS_MAX_FILTERS; ff++) r->tns_length[w][ff] = reemit_read_i32(f);
    for (w = 0; w < REEMIT_MAX_WINDOWS; w++)
        for (ff = 0; ff < TNS_MAX_FILTERS; ff++) r->tns_order[w][ff] = reemit_read_i32(f);
    for (w = 0; w < REEMIT_MAX_WINDOWS; w++)
        for (ff = 0; ff < TNS_MAX_FILTERS; ff++) r->tns_direction[w][ff] = reemit_read_i32(f);
    for (w = 0; w < REEMIT_MAX_WINDOWS; w++)
        for (ff = 0; ff < TNS_MAX_FILTERS; ff++) r->tns_compress[w][ff] = reemit_read_i32(f);
    for (w = 0; w < REEMIT_MAX_WINDOWS; w++)
        for (ff = 0; ff < TNS_MAX_FILTERS; ff++)
            for (i = 0; i < TNS_MAX_ORDER; i++) r->tns_coef[w][ff][i] = reemit_read_i32(f);
}

/* Fills one channel's CoderInfo entirely from a ReemitICS record: no psy,
 * no MDCT, no TNS analysis, no stereo decision, no rate loop. Mirrors just
 * enough of frame.c's normal per-channel setup (block_type/sfbn/sfb_offset/
 * groups, ResetCoderSections, assign-then-huffbook) for WriteBitstream to
 * produce a real, decodable AAC element from decisions that are entirely
 * the caller's. */
static void reemit_fill_channel(struct faacEncStruct *henc, int ch, const ReemitICS *rec)
{
    CoderInfo *ci = &henc->coderInfo[ch];
    int qs[FRAME_LEN];
    int qlen = 0;
    int b, g, w, absw;

    ci->partner = NULL;
    ci->useRef = 0;
    ci->msUsed = NULL;
    ci->msPeer = NULL;
    memset(ci->msEl, 0, sizeof(ci->msEl));

    ci->block_type = rec->win_seq;
    ci->window_shape = rec->window_shape;
    ci->sfbn = rec->max_sfb;
    ci->sfb_offset = (rec->win_seq == ONLY_SHORT_WINDOW) ? henc->sfbOffsetShort : henc->sfbOffsetLong;
    ci->groups.n = rec->num_groups;
    for (g = 0; g < rec->num_groups; g++)
        ci->groups.len[g] = rec->group_len[g];
    ci->global_gain = rec->global_gain;
    ci->bandcnt = rec->num_bands;
    ci->datacnt = 0;

    for (b = 0; b < rec->num_bands; b++)
        ci->sf[b] = rec->band[b].sf;

    /* Band-major qs[], the order huffbook()/huffcode_write() expect: per
     * group, per regular (cb 1-11) sfb, concatenated over that group's raw
     * windows, in window-major then bin order -- the same order
     * assign_band_codebooks/BlocQuant build it in from a real MDCT, so this
     * is a direct re-layout of rec->quantized (window-major) rather than a
     * reinterpretation of its values. PNS/zero/intensity bands (cb 0,13,14,
     * 15) contribute no entries: the real spectral-data syntax transmits
     * none for them either.
     *
     * book[] for a regular band is recomputed from the ACTUAL maxq in
     * rec->quantized here, exactly like Step1Quantize -- not trusted from
     * rec->band[b].cb. For a plain Control-0 re-emit (rec->quantized is the
     * same reference's own values) this is a no-op, since a reference's own
     * cb always already covers its own maxq by construction. It matters
     * once a caller (e.g. probe/ladder/hybrid_main.c's line-substitution
     * arms) hands in a `quantized[]` that isn't the same reference the
     * preset cb came from: without this, huffbook()'s Viterbi can index its
     * cost tables out of range for a too-small preset family and silently
     * pick an invalid book -- the exact bitstream-desync bug found and
     * fixed in step1.c earlier this session. */
    absw = 0;
    for (g = 0; g < rec->num_groups; g++) {
        int glen = rec->group_len[g];
        int sfb;
        for (sfb = 0; sfb < rec->max_sfb; sfb++) {
            int band = g * rec->max_sfb + sfb;
            int cb = rec->band[band].cb;
            if (cb < HCB_1 || cb > HCB_ESC) {
                ci->book[band] = cb; /* 0, 13, 14 or 15: pass through verbatim */
                continue;
            }
            int lo = ci->sfb_offset[sfb], hi = ci->sfb_offset[sfb + 1];
            int maxq = 0;
            int band_start = qlen;
            for (w = 0; w < glen; w++) {
                int base = (rec->win_seq == ONLY_SHORT_WINDOW) ? (absw + w) * 128 : 0;
                int k;
                for (k = lo; k < hi; k++) {
                    int q = rec->quantized[base + k];
                    int a = q < 0 ? -q : q;
                    if (a > maxq) maxq = a;
                    qs[qlen++] = q;
                }
            }
            if (!maxq) {
                ci->book[band] = HCB_ZERO;
                qlen = band_start;
            } else {
                ci->book[band] = maxq <= LAV_1 ? HCB_1 : maxq <= LAV_2 ? HCB_3
                               : maxq <= LAV_4 ? HCB_5 : maxq <= LAV_7 ? HCB_7
                               : maxq <= LAV_12 ? HCB_9 : HCB_ESC;
            }
        }
        absw += glen;
    }
    huffbook(ci, qs);

    ReemitSetTnsInfo(ci, rec);
}

/* Populates ci->tnsInfo (tnsDataPresent, probeSyntax, windowData or
 * probeWindowData[]) straight from a ReemitICS's already-quantized filters
 * -- for the writer to transmit verbatim. Shared between the Control-0
 * re-emit path (above) and step1.c, which needs the exact same bitstream-
 * facing fields set after it has separately applied the same filters to
 * FAAC's real spectrum (step1.c's Step1ApplyTns does the spectral half;
 * this does the syntax half; the two must agree on every field). */
void ReemitSetTnsInfo(CoderInfo *ci, const ReemitICS *rec)
{
    ci->tnsInfo.tnsDataPresent = rec->tns_present;
    ci->tnsInfo.probeSyntax = 1;
    if (rec->tns_present) {
        int nwin = (rec->win_seq == ONLY_SHORT_WINDOW) ? MAX_SHORT_WINDOWS : 1;
        TnsWindowData *dst0 = &ci->tnsInfo.windowData;
        int w;
        for (w = 0; w < nwin; w++) {
            TnsWindowData *dst = (rec->win_seq == ONLY_SHORT_WINDOW)
                                ? &ci->tnsInfo.probeWindowData[w] : dst0;
            int f;
            dst->numFilters = rec->tns_num_filt[w];
            /* rec->tns_coef_res[w] is the reference decoder's raw 1-bit
             * coef_res field (0/1); TnsWindowData.coefResolution is FAAC's
             * absolute convention (3 or 4 bits/coefficient) -- WriteICS
             * writes back coefResolution - DEF_TNS_RES_OFFSET, so storing
             * the raw bit directly here would write a negative value into a
             * 1-bit field and corrupt the rest of the ICS. */
            dst->coefResolution = rec->tns_coef_res[w] + DEF_TNS_RES_OFFSET;
            for (f = 0; f < rec->tns_num_filt[w] && f < TNS_MAX_FILTERS; f++) {
                TnsFilterData *flt = &dst->tnsFilter[f];
                int k;
                flt->length = rec->tns_length[w][f];
                flt->order = rec->tns_order[w][f];
                flt->direction = rec->tns_direction[w][f];
                flt->coefCompress = rec->tns_compress[w][f];
                /* rec->tns_coef[][][] is 0-indexed (c = 0..order-1, straight
                 * off the FAAD_LADDER_DUMP 'I' record); TnsFilterData.index[]
                 * is 1-indexed (tns.c fills index[1..order], index[0] unused
                 * -- see quantize_coeffs's callers). */
                for (k = 1; k <= flt->order && k <= TNS_MAX_ORDER; k++)
                    flt->index[k] = rec->tns_coef[w][f][k - 1];
            }
        }
    } else {
        ci->tnsInfo.windowData.numFilters = 0;
    }
}

/* Populates el->common_window/msInfo from two channels' records, and links
 * their CoderInfo::partner -- the CPE-level half of setting up a reemit or
 * step1 frame, shared between both paths. */
void ReemitSetCpeInfo(AACElement *el, CoderInfo *ciL, CoderInfo *ciR,
                       const ReemitICS *left, const ReemitICS *right)
{
    int common = right && right->present
               && left->win_seq == right->win_seq
               && left->max_sfb == right->max_sfb
               && left->num_groups == right->num_groups;
    if (common) {
        int g;
        for (g = 0; g < left->num_groups && common; g++)
            if (left->group_len[g] != right->group_len[g]) common = 0;
    }
    el->common_window = common;
    if (common) {
        int n = left->num_groups * left->max_sfb, i;
        int any_ms = 0;
        for (i = 0; i < n && i < MAX_SCFAC_BANDS; i++) {
            el->msInfo.ms_used[i] = (uint8_t)left->band[i].ms;
            any_ms |= left->band[i].ms;
        }
        el->msInfo.is_present = any_ms ? 1 : 0;
    } else {
        el->msInfo.is_present = 0;
    }
    ciL->partner = ciR;
}

int faacEncReemitFrame(faacEncHandle hEncoder, const ReemitICS *ch0,
                        const ReemitICS *ch1,
                        unsigned char *outputBuffer, unsigned int bufferSize)
{
    struct faacEncStruct *henc = (struct faacEncStruct *)hEncoder;
    BitStream *bs;
    int frameBytes;

    if (!henc || !ch0 || !ch0->present)
        return -1;

    reemit_fill_channel(henc, 0, ch0);
    if (ch1 && ch1->present)
        reemit_fill_channel(henc, 1, ch1);

    if (henc->numElements >= 1 && henc->elements[0].type == ID_CPE && ch1 && ch1->present) {
        AACElement *el = &henc->elements[0];
        ReemitSetCpeInfo(el, &henc->coderInfo[el->channels[0]],
                          &henc->coderInfo[el->channels[1]], ch0, ch1);
    }

    bs = OpenBitStream(bufferSize, outputBuffer);
    if (!bs)
        return -1;
    if (WriteBitstream(henc, henc->coderInfo, henc->elements, henc->numElements, bs) < 0)
        return -1;
    frameBytes = CloseBitStream(bs);
    return frameBytes;
}
