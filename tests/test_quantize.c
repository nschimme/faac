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

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <string.h>
#include "libfaac/coder.h"
#include "../libfaac/quantize.c"

void test_CalcBW() {
    AACQuantCfg cfg;
    SR_INFO sr;
    unsigned int bw = 15000;

    /* Mock SR_INFO for fixed sample rate logic */
    memset(&sr, 0, sizeof(sr));
    sr.num_cb_short = 10;
    sr.num_cb_long = 40;
    for(int i=0; i<10; i++) sr.cb_width_short[i] = 4;
    for(int i=0; i<40; i++) sr.cb_width_long[i] = 32;

    cfg.pnslevel = 0;
    CalcBW(&bw, 44100, &sr, &cfg);

    /* Bandwidth limit accounting for transform length */
    assert(cfg.max_cbs == 10);
    assert(cfg.max_cbl == 22);
}

void test_quantize_scalar() {
    faac_real xr[4] = {1.0, -2.0, 0.5, 0.0};
    int xi[4];

    /* Verify non-linear quantization: q = sign(x) * floor((|x| * sfacfix)^0.75 + MAGIC) */
    quantize_scalar(xr, xi, 4, 1.0);

    assert(xi[0] == 1);
    assert(xi[1] == -2);
}

int main() {
    QuantizeInit();
    test_CalcBW();
    test_quantize_scalar();
    printf("test_quantize passed\n");
    return 0;
}
