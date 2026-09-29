/* Probe-only (ladder step 1: FAAC's own MDCT quantized at a reference's
 * exact per-ICS decisions -- window/grouping forced before the real MDCT,
 * M/S and TNS applied to FAAC's real spectrum, then quantized at the
 * reference's absolute scalefactors/global_gain with FAAC's own quantizer
 * and huffbook(). No psy, no rate loop, no natural class/M-S/TNS search.
 * Shares the ReemitICS record format (reemit.h) and FAAD_LADDER_DUMP-derived
 * binary intermediate (parse_dump.py) with the Control-0 re-emit path;
 * step1's job differs only in HOW those decisions are realized: written
 * straight to the bitstream by reemit.c, or applied to FAAC's own spectrum
 * and re-derived by quantization here. */
#ifndef STEP1_H
#define STEP1_H

#include "coder.h"
#include "reemit.h"

#ifdef __cplusplus
extern "C" {
#endif

struct Step1Ctx;

/* Lazy process-wide singleton, loaded once from FAAC_STEP1=<intermediate.bin>
 * (same wire format parse_dump.py produces for reemit) and FAAC_STEP1_OFFSET
 * =<n> (ciFrame-space offset; see probe/ladder/mdct_offset.py -- established
 * as +2 for both references on this corpus). Returns NULL (cheaply) when
 * FAAC_STEP1 is unset. */
struct Step1Ctx *Step1Get(void);

/* Looks up the record at ciFrame + configured offset for channel ch. Returns
 * 1 and fills *out if present, 0 (out left untouched) otherwise. */
int Step1Lookup(struct Step1Ctx *s1, int ciFrame, int ch, ReemitICS *out);

/* Applies rec's already-quantized TNS filters to ci's real spectrum, in
 * FAAC's own analysis (whitening) sign convention -- mirrors libfaad/tns.c's
 * apply_tns() region-stacking (filters stack downward from the channel's
 * full sfb-table size, clipped to max_sfb and the profile's TNS_MAX_BANDS
 * table) and direction handling exactly, so what this whitens is exactly
 * what a decoder's inverse would un-whiten. sr_idx is the encoder's
 * sampleRateIdx (indexes the same ISO Table 4.139 TNS_MAX_BANDS this
 * project's own libfaad/tns.c already implements). ci->block_type/sfbn/
 * sfb_offset must already reflect rec's forced window/grouping. */
void Step1ApplyTns(CoderInfo *ci, float *spec, const ReemitICS *rec, int sr_idx);

/* True M/S butterfly (FAAC's own 0.5*(L+R)/0.5*(L-R) convention, exactly
 * inverted by a decoder's l=m+s/r=m-s) on specL/specR, where the ms flag
 * is set and both channels are regular. In self_mode a band with one side
 * subsequently reclassed ZERO is also transformed, matching AACstereo's
 * earlier spectrum change. sfb_offset/max_sfb/groups must match. */
void Step1ApplyMS(const ReemitICS *left, const ReemitICS *right,
                   const int *sfb_offset, float *specL, float *specR,
                   int self_mode);

/* Reproduce AACstereo's intensity transform and left-channel SF bias for a
 * FAAC self-reference. External references preserve the KA baseline. */
void Step1ApplySelfIS(const ReemitICS *left, const ReemitICS *right,
                      const int *sfb_offset, float *specL, float *specR,
                      int *left_sf_bias);

/* Quantizes ci's (already TNS/M-S-adjusted) spectrum at rec's absolute
 * per-band class/scalefactor/global_gain, using FAAC's own qfunc-equivalent
 * gain formula (sfac_to_gain's overflow clamp included) and the real
 * huffbook() for final book assignment. PNS/zero/intensity bands are
 * classed directly with no spectral quantization (they carry no coefficient
 * data in the bitstream either). A regular band whose quantized magnitudes
 * are all zero is reclassed HCB_ZERO -- required for bitstream validity, not
 * optional -- and *zero_count is incremented once per such occurrence. */
/* scale: uniform gain multiplier, 1.0 for a normal call. See step1.c for
 * when frame.c's retry loop passes something smaller. */
void Step1Quantize(CoderInfo *ci, float *spec, const ReemitICS *rec, int *zero_count,
                    float scale, const int *sf_bias);

/* Legacy probe counter; matched transition windows are now forced. */
long Step1TransitionSkippedCount(void);

/* Total frames (process-wide) that needed the uniform-attenuation overflow
 * fallback -- see frame.c's retry loop and Step1Quantize's `scale` param. */
long Step1OverflowFrameCount(void);

#ifdef __cplusplus
}
#endif

#endif /* STEP1_H */
