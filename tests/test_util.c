#include <stdio.h>
#include <assert.h>
#include <math.h>
#include "libfaac/util.h"

void test_GetSRIndex() {
    // Exact nominal rates
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

    // Non-table values (slight offsets)
    assert(GetSRIndex(44200) == 4);  // 44100 is 4th index (range starts at 46009)
    // Wait, let's look at the ranges in GetSRIndex in util.c:
    // 46009 <= rate -> 3
    // 37566 <= rate -> 4
    // 44200 is between 37566 and 46009, so it should be index 4.

    // Out of range (high)
    assert(GetSRIndex(100000) == 0);

    // Out of range (low)
    assert(GetSRIndex(4000) == 11);
    assert(GetSRIndex(0) == 11);
}

void test_MinBitrate() {
    assert(MinBitrate() == 8000);
}

void test_MaxBitrate() {
    unsigned int rate = MaxBitrate(44100);
    // 0x2000 * 8 * 44100 / 1024 = 8192 * 8 * 44100 / 1024 = 65536 * 44100 / 1024 = 64 * 44100 = 2822400
    assert(rate == 2822400);
}

void test_BitAllocation() {
    // PE = 0
    assert(BitAllocation(0.0, 0) == 0);
    assert(BitAllocation(0.0, 1) == 0);

    // Large PE should be capped at 6144
    assert(BitAllocation(1000000.0, 0) == 6144);
    assert(BitAllocation(1000000.0, 1) == 6144);

    // Some intermediate value
    // For long block: 0.3 * 100 + 6.0 * sqrt(100) = 30 + 60 = 90
    assert(BitAllocation(100.0, 0) == 90);
    // For short block: 0.6 * 100 + 24.0 * sqrt(100) = 60 + 240 = 300
    assert(BitAllocation(100.0, 1) == 300);
}

void test_MaxBitresSize() {
    // Standard case: 128kbps at 44.1kHz
    unsigned int size = MaxBitresSize(128000, 44100);
    // 128000 / 44100 * 1024 = 2.902 * 1024 = 2972.1
    // 6144 - 2972 = 3172
    assert(size == 3172);

    // Edge case where bits per frame > 6144
    // bitRate = 6144 * 44100 / 1024 = 264600 bps
    unsigned int size2 = MaxBitresSize(264600, 44100);
    assert(size2 == 0);

    // If it's even higher, it wraps around (as per the code 6144 - unsigned int)
    // Actually, (unsigned int)(...) happens first.
    // 529200 bps -> 12288 bits/frame
    // 6144 - 12288 = (unsigned int)-6144
    unsigned int size3 = MaxBitresSize(529200, 44100);
    assert(size3 > 6144); // This might be a bug in the code, but the test documents current behavior
}

int main() {
    test_GetSRIndex();
    test_MinBitrate();
    test_MaxBitrate();
    test_BitAllocation();
    test_MaxBitresSize();
    printf("test_util passed\n");
    return 0;
}
