#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <string.h>
#include "libfaac/coder.h"
#include "libfaac/filtbank.h"
#include "libfaac/fft.h"
#include "../libfaac/filtbank.c"

void test_CalculateKBDWindow() {
    faac_real win[256];
    CalculateKBDWindow(win, 4, 256);

    /* Verify Kaiser-Bessel Derived (KBD) properties: monotonic increasing in first half */
    for(int i=1; i<128; i++) {
        assert(win[i] >= win[i-1]);
    }
    assert(win[0] >= 0.0);
    assert(win[127] <= 1.0);
    assert(win[127] > 0.9);
}

void test_MDCT() {
    FFT_Tables tables;
    fft_initialize(&tables);

    int N = 256;
    faac_real data[256];
    faac_real xr[64];
    faac_real xi[64];

    /* Stability check for DC signal transform */
    for(int i=0; i<256; i++) data[i] = 1.0;

    MDCT(&tables, data, N, xr, xi);

    fft_terminate(&tables);
}

int main() {
    test_CalculateKBDWindow();
    test_MDCT();
    printf("test_mdct passed\n");
    return 0;
}
