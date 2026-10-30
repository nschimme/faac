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

#include <assert.h>
#include <math.h>

#include "drc.h"
#include "frame.h"
#include "channels.h"
#include "bitstream.h"

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
     */
    int sgn = 0;
    int ctl = 0;
    float gain = drc->gain_db;

    if (gain < 0.0f) {
        sgn = 1; /* 1 = cut / compression per ISO/IEC 14496-3 §4.5.2.1 */
        ctl = (int)(-gain / 0.25f + 0.5f);
    } else {
        sgn = 0; /* 0 = boost */
        ctl = (int)(gain / 0.25f + 0.5f);
    }
    if (ctl > 127)
        ctl = 127;

    int ref_code = 0;
    if (drc->prog_ref_level_present) {
        float ref_val = fabsf(drc->prog_ref_level_db);
        ref_code = (int)(ref_val / 0.25f + 0.5f);
        if (ref_code > 127)
            ref_code = 127;
    }

    uint32_t payloadBits = 4 + 4 + (drc->prog_ref_level_present ? 8 : 0) + 8;
    int fillBytes = (payloadBits + 7) / 8;

    assert(fillBytes <= 14 + 255);

    if (bs) {
        uint32_t startBit = bs->currentBit;

        /* Write fill_element header (ISO/IEC 14496-3 Table 4.11) */
        PutBit(bs, ID_FIL, 3);
        PutBit(bs, fillBytes < 15 ? fillBytes : 15, 4);
        if (fillBytes >= 15)
            PutBit(bs, fillBytes - 14, 8);

        /* Write extension_payload: extension_type EXT_DYNAMIC_RANGE (0xB) */
        PutBit(bs, EXT_DYNAMIC_RANGE, 4);

        /* Write dynamic_range_info() fields */
        PutBit(bs, 0, 1); /* pce_tag_present = 0 */
        PutBit(bs, 0, 1); /* excluded_chns_present = 0 */
        PutBit(bs, 0, 1); /* drc_bands_present = 0 */
        PutBit(bs, drc->prog_ref_level_present ? 1 : 0, 1);

        if (drc->prog_ref_level_present) {
            PutBit(bs, ref_code, 7);
            PutBit(bs, 0, 1); /* prog_ref_level_reserved_bits = 0 */
        }

        /* Band 0 dynamic range gain: dyn_rng_sgn (1) + dyn_rng_ctl (7) */
        PutBit(bs, sgn, 1);
        PutBit(bs, ctl, 7);

        /* Pad payload to full bytes */
        uint32_t writtenBits = bs->currentBit - (startBit + (fillBytes < 15 ? 7 : 15));
        if (writtenBits < (uint32_t)fillBytes * 8)
            PutBit(bs, 0, fillBytes * 8 - writtenBits);
    }

    int headerBits = (fillBytes < 15) ? 7 : 15;
    return headerBits + fillBytes * 8;
}

#if FAAC_ENCODER_DRC

void DrcInit(faacEncStruct *hEncoder)
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
                                                hEncoder->config.drcProfile, LOOKAHEAD_DEPTH - 1);
    }
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
        DrcAnalyzerEnd(hEncoder->drcAnalyzer);
        hEncoder->drcAnalyzer = NULL;
    }
}

#endif
