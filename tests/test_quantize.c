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

    memset(&sr, 0, sizeof(sr));
    sr.num_cb_short = 10;
    sr.num_cb_long = 40;
    for(int i=0; i<10; i++) sr.cb_width_short[i] = 4;
    for(int i=0; i<40; i++) sr.cb_width_long[i] = 32;

    cfg.pnslevel = 0;
    CalcBW(&bw, 44100, &sr, &cfg);

    assert(cfg.max_cbs == 10);
    assert(cfg.max_cbl == 22);
}

void test_quantize_scalar() {
    faac_real xr[4] = {1.0, -2.0, 0.5, 0.0};
    int xi[4];

    // sfacfix = 1.0
    quantize_scalar(xr, xi, 4, 1.0);

    // logic: tmp = |val| * sfacfix; tmp = (tmp * sqrt(tmp))^(1/2) = tmp^(3/4)
    // Wait, code says: tmp = FAAC_SQRT(tmp * FAAC_SQRT(tmp));
    // That is sqrt(tmp * tmp^0.5) = sqrt(tmp^1.5) = tmp^0.75.
    // q = (int)(tmp + 0.4054)

    // 1.0^0.75 = 1.0. q = floor(1.4054) = 1. xi[0] = 1.
    assert(xi[0] == 1);
    // 2.0^0.75 = 1.6817. q = floor(1.6817 + 0.4054) = floor(2.087) = 2. xi[1] = -2.
    assert(xi[1] == -2);
}

int main() {
    QuantizeInit();
    test_CalcBW();
    test_quantize_scalar();
    printf("test_quantize passed\n");
    return 0;
}
