#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <string.h>
#include "libfaac/coder.h"
#include "../libfaac/tns.c"

void test_Autocorrelation() {
    faac_real data[10] = {1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    faac_real r[5];

    Autocorrelation(4, 5, data, r);
    /* Unit impulse: R(0)=1, R(k)=0 */
    assert(fabs(r[0] - 1.0) < 1e-6);
    assert(fabs(r[1] - 0.0) < 1e-6);
    assert(fabs(r[2] - 0.0) < 1e-6);
    assert(fabs(r[3] - 0.0) < 1e-6);
    assert(fabs(r[4] - 0.0) < 1e-6);

    faac_real data2[10] = {1.0, 1.0, 1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    Autocorrelation(2, 5, data2, r);
    /* Rectangular pulse: R(k) = (dataSize - k) */
    assert(fabs(r[0] - 5.0) < 1e-6);
    assert(fabs(r[1] - 4.0) < 1e-6);
    assert(fabs(r[2] - 3.0) < 1e-6);
}

void test_LevinsonDurbin() {
    faac_real data[10] = {1.0, 0.5, 0.25, 0.125, 0.0625, 0.0, 0.0, 0.0, 0.0, 0.0};
    faac_real k[5];
    faac_real gain;

    gain = LevinsonDurbin(2, 5, data, k);
    /* Prediction gain must be positive for correlated signal */
    assert(gain > 1.0);
    assert(k[0] == 1.0);
}

void test_StepUp() {
    faac_real k[3] = {1.0, 0.5, 0.5};
    faac_real a[3];

    StepUp(2, k, a);
    /* Verify Levinson recursion: a_i(j) = a_{i-1}(j) + k_i * a_{i-1}(i-j) */
    assert(a[0] == 1.0);
    assert(fabs(a[1] - 0.75) < 1e-6);
    assert(fabs(a[2] - 0.5) < 1e-6);
}

int main() {
    test_Autocorrelation();
    test_LevinsonDurbin();
    test_StepUp();
    printf("test_tns passed\n");
    return 0;
}
