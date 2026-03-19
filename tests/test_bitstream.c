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
    // Actually, PutBit handles offsets.
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

int main() {
    test_PutBit();
    printf("test_bitstream passed\n");
    return 0;
}
