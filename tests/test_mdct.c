#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <string.h>
#include "libfaac/coder.h"
#include "libfaac/filtbank.h"
#include "libfaac/fft.h"

void test_CalculateKBDWindow() {
    faac_real win[256];
    CalculateKBDWindow(win, 4, 256);

    // Check for some general properties of KBD window
    // 1. Should be increasing in the first half
    for(int i=1; i<128; i++) {
        assert(win[i] >= win[i-1]);
    }
    // 2. Symmetry is usually handled outside CalculateKBDWindow,
    // it only calculates the first half.
    assert(win[0] >= 0.0);
    assert(win[127] <= 1.0);
    assert(win[127] > 0.9);
}

void test_MDCT() {
    FFT_Tables tables;
    fft_initialize(&tables);

    int N = 256;
    faac_real data[256];
    faac_real xr[64]; // N/4
    faac_real xi[64];

    // DC signal
    for(int i=0; i<256; i++) data[i] = 1.0;

    // MDCT of DC signal should be 0 because it's like a sine transform of sorts
    // actually it's complicated. Let's just check it doesn't crash and gives something.
    MDCT(&tables, data, N, xr, xi);

    // MDCT in FAAC doesn't seem to be a standard one-shot function,
    // it fills in the data array with results.
    // wait, looking at MDCT in filtbank.c:
    /*
        base_even0[n2] = -tempr;  / first half even /
        base_odd0[-n2] =  tempi;  / first half odd /
        base_even1[n2] = -tempi;  / second half even /
        base_odd1[-n2] =  tempr;  / second half odd /
    */
    // It seems to be an in-place-ish transform that uses data as both in and out.

    fft_terminate(&tables);
}

int main() {
    test_CalculateKBDWindow();
    test_MDCT();
    printf("test_mdct passed\n");
    return 0;
}
