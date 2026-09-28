/* Probe-only (ladder Control 0 / step 1). Drives FAAC's real bitstream
 * writer (WriteBitstream/huffbook/WriteICS in channels.c/huff2.c) directly
 * from a reference decoder's per-ICS syntax, bypassing FAAC's own psy model,
 * MDCT, TNS analysis, stereo decision and rate loop entirely. Used to test
 * whether FAAC's writer can faithfully represent an arbitrary (valid) AAC
 * LC bitstream, decoupled from whether FAAC's own analysis would ever have
 * chosen those decisions.
 *
 * Never used by faacEncEncode's normal path; only probe/ladder tools call
 * faacEncReemitFrame directly, after faacEncOpen() has built a normal
 * encoder handle (for its config/elements/sfb tables) that this bypasses
 * per-frame analysis on. */
#ifndef REEMIT_H
#define REEMIT_H

#include <stdio.h>
#include "coder.h"
#include "channels.h"
#include "faac_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

#define REEMIT_MAX_WINDOWS MAX_SHORT_WINDOWS

typedef struct {
    int cb;   /* codebook: 0=zero, 1-11=regular, 13=PNS, 14/15=intensity */
    int sf;   /* absolute scalefactor (0-255), meaningless when cb==0 */
    int ms;   /* ms_used flag for this band, 0/1 (CPE common-window only) */
} ReemitBand;

/* One channel's full ICS, as decoded straight off a reference bitstream.
 * win_seq is the raw ISO window_sequence value (matches FAAC's block_type
 * enum numerically: ONLY_LONG/LONG_SHORT/ONLY_SHORT/SHORT_LONG = 0/1/2/3).
 * `quantized[]` holds the RAW (pre-dequantization, pre-pow(4/3)), signed
 * Huffman-decoded spectral values in the same window-major layout FAAD3
 * stores them (index = window*128+bin for short windows, index = bin
 * directly for long windows) -- i.e. copy straight from a FAAD_LADDER_DUMP
 * 'Q' record, no reordering needed by the caller.
 *
 * TNS is per RAW window (0..7), even under grouping: index tns_* arrays by
 * absolute window number, not by group. For long windows only index 0 is
 * used. tns_coef_res is the ABSOLUTE resolution (3 or 4 bits/coefficient),
 * i.e. the reference decoder's raw 1-bit field plus 3 -- see tns.c's
 * DEF_TNS_RES_OFFSET. tns_coef[][][] are the raw signed reflection-
 * coefficient indices exactly as the decoder stored them (already sign-
 * extended to the transmitted bit width); they are written back through the
 * same mask-to-width path FAAC's own TNS writer uses, so no further sign
 * handling is needed by the caller. */
typedef struct {
    int present;
    int win_seq;
    int window_shape;      /* 0 sine, 1 KBD; only meaningful once the writer
                             * supports non-sine shapes -- ignored (assumed
                             * sine) until then. */
    int max_sfb;
    int num_groups;
    int group_len[REEMIT_MAX_WINDOWS];
    int global_gain;
    int num_bands;          /* num_groups * max_sfb */
    ReemitBand band[MAX_SCFAC_BANDS];
    int quantized[FRAME_LEN];

    int tns_present;
    int tns_num_filt[REEMIT_MAX_WINDOWS];
    int tns_coef_res[REEMIT_MAX_WINDOWS];
    int tns_length[REEMIT_MAX_WINDOWS][TNS_MAX_FILTERS];
    int tns_order[REEMIT_MAX_WINDOWS][TNS_MAX_FILTERS];
    int tns_direction[REEMIT_MAX_WINDOWS][TNS_MAX_FILTERS];
    int tns_compress[REEMIT_MAX_WINDOWS][TNS_MAX_FILTERS];
    int tns_coef[REEMIT_MAX_WINDOWS][TNS_MAX_FILTERS][TNS_MAX_ORDER];
} ReemitICS;

/* ch1 may be NULL for a single SCE/LFE element. Returns the number of bytes
 * written (as faacEncEncode does), or -1. hEncoder must already be open
 * with the matching numChannels/sampleRate/outputFormat (ADTS) -- only its
 * config/elements/sfb-offset tables and rc/sbrContext plumbing WriteBitstream
 * touches are used; its FIFO, psy and coderInfo analysis state are untouched
 * and unused. */
int faacEncReemitFrame(faacEncHandle hEncoder, const ReemitICS *ch0,
                        const ReemitICS *ch1,
                        unsigned char *outputBuffer, unsigned int bufferSize);

/* Reads one ReemitICS record from `f` in parse_dump.py's fixed field order
 * (present, win_seq, ..., tns_coef[][][]), all little-endian int32, exactly
 * as written by ICS.pack(). Shared between reemit_main.c (Control 0's
 * streaming driver) and step1.c (which loads the whole file into memory) so
 * the wire format has exactly one reader. Exits the process on truncated
 * input (a malformed intermediate is a probe-tooling bug, not a condition
 * either caller can usefully recover from). */
void ReemitReadICS(FILE *f, ReemitICS *r);

/* Sets ci->tnsInfo (tnsDataPresent/probeSyntax/windowData or
 * probeWindowData[]) from rec's already-quantized TNS filters, for the
 * writer to transmit verbatim. Shared between reemit.c (the only thing that
 * touches ci->tnsInfo) and step1.c (which separately applies the same
 * filters to FAAC's real spectrum via Step1ApplyTns, then calls this for the
 * bitstream-facing half). */
void ReemitSetTnsInfo(CoderInfo *ci, const ReemitICS *rec);

/* Sets el->common_window/msInfo from two channels' records and links
 * ciL->partner = ciR. Shared between reemit.c and step1.c. */
void ReemitSetCpeInfo(AACElement *el, CoderInfo *ciL, CoderInfo *ciR,
                       const ReemitICS *left, const ReemitICS *right);

#ifdef __cplusplus
}
#endif

#endif /* REEMIT_H */
