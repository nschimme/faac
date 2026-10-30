/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2026 Nils Schimmelmann
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */

#include "drc.h"
#include "frame.h"
#include "channels.h"
#include "bitstream.h"
#include "util.h"

int DrcGetBits(const DRCInfo *drc, BitStream *bs)
{
    if (!drc || !drc->enabled)
        return 0;

    /*
     * ISO/IEC 14496-3 §4.5.2.1 (dynamic_range_info) syntax and gain semantics:
     *   - dyn_rng_sgn (1 bit): 1 = cut / compression (negative gain in dB),
     *                          0 = boost (positive gain in dB).
     *   - dyn_rng_ctl (7 bits): magnitude in 0.25 dB steps (range 0.00 to 31.75 dB).
     *   - prog_ref_level (7 bits): program reference level in -0.25 dB steps (0 to -31.75 dBFS).
     *
     * The analyzer stores both values on that grid, so the wire fields are
     * the stored magnitudes.
     */
    int gain_code = drc->gain_code;
    int sgn = gain_code < 0 ? 1 : 0;
    int ctl = sgn ? -gain_code : gain_code;
    int ref_code = drc->ref_code;

    if (ctl > 127)
        ctl = 127;

    /* The fixed payload is exactly 3 bytes: extension_type (4 bits),
     * four presence flags (4), prog_ref_level plus reserved (8), and one
     * gain (8). It always fits the short fill_element form. */
    const int fillBytes = 3;

    if (bs) {
        /* Write fill_element header (ISO/IEC 14496-3 Table 4.11) */
        PutBit(bs, ID_FIL, 3);
        PutBit(bs, fillBytes, 4);

        /* Write extension_payload: extension_type EXT_DYNAMIC_RANGE (0xB) */
        PutBit(bs, EXT_DYNAMIC_RANGE, 4);

        /* Write dynamic_range_info() fields */
        PutBit(bs, 0, 1); /* pce_tag_present = 0 */
        PutBit(bs, 0, 1); /* excluded_chns_present = 0 */
        PutBit(bs, 0, 1); /* drc_bands_present = 0 */
        PutBit(bs, 1, 1); /* prog_ref_level_present = 1 */
        PutBit(bs, ref_code, 7);
        PutBit(bs, 0, 1); /* prog_ref_level_reserved_bits = 0 */

        /* Band 0 dynamic range gain: dyn_rng_sgn (1) + dyn_rng_ctl (7) */
        PutBit(bs, sgn, 1);
        PutBit(bs, ctl, 7);

        /* The fields above are exactly fillBytes * 8 bits; no padding. */
    }

    return 7 + fillBytes * 8;
}

#if FAAC_ENCODER_DRC

int DrcInit(faacEncStruct *hEncoder)
{
    if (hEncoder->config.drcProfile != DRC_OFF) {
        /*
         * Note: hEncoder->sampleRate holds the halved core sample rate for HE-AAC,
         * but ITU-R BS.1770 K-weighting loudness measurement operates on full-rate PCM.
         */
        uint32_t fullRate = (uint32_t)SbrContextGetFullRate(hEncoder->sbrContext, hEncoder->sampleRate);
        /* The access unit written on a tick carries audio that entered the FIFO
         * LOOKAHEAD_DEPTH ticks earlier, on the encoder timeline that
         * encoder_delay also uses. The analyzer's lookahead and the tick on
         * which the 800 ms meter first sees an onset make up the difference, so
         * the gain still lands one frame ahead of a loud onset. */
        hEncoder->drcAnalyzer = DrcAnalyzerInit(fullRate, hEncoder->numChannels,
                                                hEncoder->isLfeChannel, hEncoder->config.drcRefLevel,
                                                hEncoder->config.drcProfile, faacFrameSamples(hEncoder));
        if (!hEncoder->drcAnalyzer)
            return -1;
    }
    return 0;
}

void DrcProcessFrame(faacEncStruct *hEncoder, float * const *input_pcm, uint32_t samples_per_ch)
{
    if (hEncoder->config.drcProfile != DRC_OFF && hEncoder->drcAnalyzer) {
        DrcAnalyzerProcessFrame(hEncoder->drcAnalyzer, input_pcm, samples_per_ch, &hEncoder->drcInfo);
    } else {
        hEncoder->drcInfo.enabled = false;
    }
}

void DrcClose(faacEncStruct *hEncoder)
{
    if (hEncoder->drcAnalyzer) {
        FreeMemory(hEncoder->drcAnalyzer);
        hEncoder->drcAnalyzer = NULL;
    }
}

#endif
