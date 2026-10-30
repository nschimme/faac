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

/* struct_size contract: init and open touch only the bytes the caller owns,
 * and queries report the bytes populated. Run under ASan to catch an over-read. */

#undef NDEBUG
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "faac.h"

int main(void)
{
    /* A baseline-sized allocation is a valid caller: init and open must not read past it. */
    faac_params *p = malloc(FAAC_PARAMS_BASELINE);
    assert(p);
    assert(faac_params_init(p, FAAC_PARAMS_BASELINE) == FAAC_OK);
    assert(p->struct_size == FAAC_PARAMS_BASELINE);
    p->sample_rate = 48000;
    p->channels = 1;
    p->bit_rate = 64000;
    faac_encoder *enc = NULL;
    assert(faac_encoder_open(p, &enc) == FAAC_OK);
    free(p);

    /* A larger caller keeps its tail and sees the library's size written back. */
    struct { faac_encoder_info v; unsigned char canary[16]; } info;
    memset(&info, 0xA5, sizeof(info));
    info.v.struct_size = sizeof(info);
    assert(faac_encoder_get_info(enc, &info.v) == FAAC_OK);
    assert(info.v.struct_size == sizeof(faac_encoder_info));
    for (unsigned i = 0; i < sizeof(info.canary); i++)
        assert(info.canary[i] == 0xA5);
    info.v.struct_size = FAAC_ENCODER_INFO_BASELINE - 1;
    assert(faac_encoder_get_info(enc, &info.v) == FAAC_ERR_INVALID_ARGUMENT);

    /* The smallest accepted destination gets exactly its baseline filled in. */
    void *small_buf = malloc(FAAC_ENCODER_INFO_BASELINE);
    assert(small_buf);
    faac_encoder_info *small_info = small_buf;
    small_info->struct_size = FAAC_ENCODER_INFO_BASELINE;
    assert(faac_encoder_get_info(enc, small_info) == FAAC_OK);
    assert(small_info->struct_size == FAAC_ENCODER_INFO_BASELINE);
    assert(small_info->sample_rate == 48000 && small_info->rate_control == FAAC_RC_ABR);
    free(small_info);
    assert(faac_encoder_close(&enc) == FAAC_OK);

    struct { faac_params v; unsigned char canary[16]; } big;
    memset(&big, 0xA5, sizeof(big));
    assert(faac_params_init(&big.v, sizeof(big)) == FAAC_OK);
    assert(big.v.struct_size == sizeof(faac_params));
    for (unsigned i = 0; i < sizeof(big.canary); i++)
        assert(big.canary[i] == 0xA5);

    faac_params small;
    assert(faac_params_init(&small, FAAC_PARAMS_BASELINE - 1) == FAAC_ERR_INVALID_ARGUMENT);
    return 0;
}
