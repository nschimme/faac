#include <stdio.h>
#include <assert.h>
#include "libfaac/util.h"

void test_GetSRIndex() {
    assert(GetSRIndex(96000) == 0);
    assert(GetSRIndex(88200) == 1);
    assert(GetSRIndex(64000) == 2);
    assert(GetSRIndex(48000) == 3);
    assert(GetSRIndex(44100) == 4);
    assert(GetSRIndex(32000) == 5);
    assert(GetSRIndex(24000) == 6);
    assert(GetSRIndex(22050) == 7);
    assert(GetSRIndex(16000) == 8);
    assert(GetSRIndex(12000) == 9);
    assert(GetSRIndex(11025) == 10);
    assert(GetSRIndex(8000) == 11);
    assert(GetSRIndex(4000) == 11);
}

void test_MinBitrate() {
    assert(MinBitrate() == 8000);
}

void test_MaxBitrate() {
    unsigned int rate = MaxBitrate(44100);
    assert(rate > 0);
}

void test_MaxBitresSize() {
    // Just a basic check that it returns something reasonable
    unsigned int size = MaxBitresSize(128000, 44100);
    assert(size <= 6144);
}

int main() {
    test_GetSRIndex();
    test_MinBitrate();
    test_MaxBitrate();
    test_MaxBitresSize();
    printf("test_util passed\n");
    return 0;
}
