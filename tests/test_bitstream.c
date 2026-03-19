#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include "libfaac/bitstream.h"

void test_PutBit() {
    unsigned char buffer[100];
    memset(buffer, 0, 100);
    BitStream *bs = OpenBitStream(100, buffer);
    assert(bs != NULL);

    // Put 8 bits: 0xAA (10101010)
    PutBit(bs, 0xAA, 8);
    assert(buffer[0] == 0xAA);
    assert(bs->numBit == 8);

    // Put 4 bits: 0x5 (0101)
    PutBit(bs, 0x5, 4);
    // Buffer should be 0xAA followed by 0x50 (since it's at the start of the next byte)
    // 10101010 (8 bits) + 0101 (4 bits) = 10101010 01010000 = 0xAA 0x50
    assert(buffer[1] == 0x50);
    assert(bs->numBit == 12);

    // Put 5 bits: 0x1F (11111)
    // 10101010 0101 (12 bits) + 11111 (5 bits) = 10101010 01011111 10000000 = 0xAA 0x5F 0x80
    PutBit(bs, 0x1F, 5);
    assert(buffer[1] == 0x5F);
    assert(buffer[2] == 0x80);
    assert(bs->numBit == 17);

    CloseBitStream(bs);
}

void test_PutBit_EdgeCases() {
    unsigned char buffer[100];
    memset(buffer, 0, 100);
    BitStream *bs = OpenBitStream(100, buffer);
    assert(bs != NULL);

    // Test writing 0 bits
    PutBit(bs, 0x1234, 0);
    assert(bs->numBit == 0);

    // Test writing 32 bits
    PutBit(bs, 0xDEADBEEF, 32);
    assert(bs->numBit == 32);
    assert(buffer[0] == 0xDE);
    assert(buffer[1] == 0xAD);
    assert(buffer[2] == 0xBE);
    assert(buffer[3] == 0xEF);

    // Test writing bits crossing multiple byte boundaries
    // Current bit position is 32.
    // Let's write 3 bits: 0x7 (111)
    PutBit(bs, 0x7, 3);
    // Position 32-34 are 111.
    // buffer[4] should have 11100000 = 0xE0
    assert(buffer[4] == 0xE0);

    // Now write 17 bits: 0x1FFFF (all 1s)
    // Position 35 to 51 (inclusive)
    PutBit(bs, 0x1FFFF, 17);
    // Position 35-39 (5 bits) in buffer[4]: should be 11111.
    // Original buffer[4] was 111 (bits 32-34).
    // So buffer[4] is 111 11111 = 0xFF
    assert(buffer[4] == 0xFF);

    // Position 40-47 (8 bits) in buffer[5]: should be 11111111 = 0xFF
    assert(buffer[5] == 0xFF);

    // Position 48-51 (4 bits) in buffer[6]: should be 11110000 = 0xF0
    assert(buffer[6] == 0xF0);

    CloseBitStream(bs);
}

int main() {
    test_PutBit();
    test_PutBit_EdgeCases();
    printf("test_bitstream passed\n");
    return 0;
}
