#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <string.h>
#include "libfaac/quantize.h"

// We can't easily test the static quantize_scalar directly as it is not exported,
// but we can call QuantizeInit() and then some other function that uses qfunc
// or just re-implement a test for the logic.
// However, the internal static library faac_internal should allow us to test
// exported functions. BlocGroup and CalcBW are exported.

void test_CalcBW() {
    AACQuantCfg cfg;
    SR_INFO sr;
    unsigned int bw = 15000;

    // Mock SR_INFO
    memset(&sr, 0, sizeof(sr));
    sr.num_cb_short = 10;
    sr.num_cb_long = 40;
    for(int i=0; i<10; i++) sr.cb_width_short[i] = 4;
    for(int i=0; i<40; i++) sr.cb_width_long[i] = 32;

    cfg.pnslevel = 0;
    CalcBW(&bw, 44100, &sr, &cfg);

    // 15000 * (1024 * 2) / 44100 = 15000 * 2048 / 44100 = 696.59 lines
    // For short frame: max = 696.59 / 8 = 87.07 lines
    // sr.cb_width_short[i] = 4, so 87.07 / 4 = 21.76 bands
    // But sr.num_cb_short = 10, so max 10 bands.
    // cfg.max_cbs should be 10.
    assert(cfg.max_cbs == 10);

    // For long frame: max = 696.59 lines
    // sr.cb_width_long[i] = 32, so 696.59 / 32 = 21.76 bands
    assert(cfg.max_cbl == 22); // Because it stops when l >= max. 21*32 = 672, 22*32 = 704.
}

int main() {
    QuantizeInit();
    test_CalcBW();
    printf("test_quantize passed\n");
    return 0;
}
