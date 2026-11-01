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

/* Unit tests for long-window temporal noise shaping. */
#include "../frame.h"
#include "../tns.h"
#undef NDEBUG
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static faacEncStruct *make_encoder(void)
{
    faacEncStruct *encoder = calloc(1, sizeof(*encoder) + sizeof(CoderInfo));
    assert(encoder != NULL);
    encoder->sampleRateIdx = 4;
    encoder->numChannels = 1;
    encoder->srInfo = &srInfo[encoder->sampleRateIdx];
    TnsInit(encoder);

    CoderInfo *coder = &encoder->coderInfo[0];
    coder->sfbn = encoder->srInfo->num_cb_long;
    coder->sfb_offset = encoder->sfbOffsetLong;
    for (int band = 0; band < coder->sfbn; band++)
        encoder->sfbOffsetLong[band + 1] = encoder->sfbOffsetLong[band]
            + encoder->srInfo->cb_width_long[band];
    assert(encoder->sfbOffsetLong[coder->sfbn] == BLOCK_LEN_LONG);
    return encoder;
}

static void test_silence_and_noise_are_skipped(void)
{
    faacEncStruct *encoder = make_encoder();
    CoderInfo *coder = &encoder->coderInfo[0];
    float spec[BLOCK_LEN_LONG] = { 0 };
    TnsEncode(coder, spec);
    assert(!coder->tnsInfo.tnsDataPresent);
    assert(coder->tnsInfo.windowData.numFilters == 0);

    unsigned state = 0x12345678u;
    for (int i = 0; i < BLOCK_LEN_LONG; i++) {
        state = state * 1664525u + 1013904223u;
        spec[i] = (float)((int32_t)(state >> 8) - 8388608) / 8388608.0f;
    }
    TnsEncode(coder, spec);
    assert(!coder->tnsInfo.tnsDataPresent);
    assert(coder->tnsInfo.windowData.numFilters == 0);
    free(encoder);
}

static void test_tonal_spectrum_emits_stable_filter(void)
{
    faacEncStruct *encoder = make_encoder();
    CoderInfo *coder = &encoder->coderInfo[0];
    float spec[BLOCK_LEN_LONG];
    float original[BLOCK_LEN_LONG];
    TnsInfo base_tns = coder->tnsInfo;
    int accepted = 0;
    for (int n = 1; n <= 50 && !accepted; n++) {
        float frequency = (float)n * 0.017f;
        CoderInfo candidate = *coder;
        candidate.tnsInfo = base_tns;
        for (int i = 0; i < BLOCK_LEN_LONG; i++) {
            spec[i] = sinf((float)i * frequency) + 0.3f * sinf((float)i * frequency * 1.7f)
                + 0.05f * cosf((float)i * frequency * 2.3f);
        }
        memcpy(original, spec, sizeof(spec));
        TnsEncode(&candidate, spec);
        if (candidate.tnsInfo.tnsDataPresent) {
            *coder = candidate;
            accepted = 1;
        }
    }
    assert(accepted);
    assert(coder->tnsInfo.windowData.numFilters == 1);
    assert(coder->tnsInfo.windowData.coefResolution == DEF_TNS_COEFF_RES);
    const TnsFilterData *filter = &coder->tnsInfo.windowData.tnsFilter[0];
    assert(filter->order > 0 && filter->order <= TNS_MAX_ORDER);
    assert(filter->length == coder->sfbn - coder->tnsInfo.tnsMinBandNumberLong);
    assert(filter->direction == 0);
    assert(filter->coefCompress == 0 || filter->coefCompress == 1);
    for (int i = 1; i <= filter->order; i++)
        assert(filter->index[i] >= -(1 << (DEF_TNS_COEFF_RES - 1))
            && filter->index[i] < (1 << (DEF_TNS_COEFF_RES - 1)));

    int start = encoder->sfbOffsetLong[coder->tnsInfo.tnsMinBandNumberLong];
    int stop = encoder->sfbOffsetLong[coder->tnsInfo.tnsMaxBandsLong];
    int changed = 0;
    for (int i = start; i < stop; i++)
        if (spec[i] != original[i]) { changed = 1; break; }
    assert(changed);
    free(encoder);
}

int main(void)
{
    test_silence_and_noise_are_skipped();
    test_tonal_spectrum_emits_stable_filter();
    return 0;
}
