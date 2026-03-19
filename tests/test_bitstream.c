#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include "libfaac/coder.h"
#include "../libfaac/bitstream.c"

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
    assert(buffer[1] == 0x50);
    assert(bs->numBit == 12);

    // Put 5 bits: 0x1F (11111)
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
    PutBit(bs, 0x7, 3);
    assert(buffer[4] == 0xE0);

    PutBit(bs, 0x1FFFF, 17);
    assert(buffer[4] == 0xFF);
    assert(buffer[5] == 0xFF);
    assert(buffer[6] == 0xF0);

    CloseBitStream(bs);
}

void test_WriteADTSHeader() {
    unsigned char buffer[100];
    memset(buffer, 0, 100);
    BitStream *bs = OpenBitStream(100, buffer);

    faacEncStruct encoder;
    memset(&encoder, 0, sizeof(encoder));
    encoder.config.mpegVersion = 0; // MPEG4
    encoder.config.aacObjectType = 2; // Low
    encoder.sampleRateIdx = 4; // 44100
    encoder.numChannels = 2;
    encoder.usedBytes = 100;

    WriteADTSHeader(&encoder, bs, 1);

    // First byte of ADTS: 0xFF
    assert(buffer[0] == 0xFF);
    // Second byte: 0xF1 (Sync 4 bits + MPEG4 1 bit + Layer 2 bits + Protection 1 bit)
    // 1111 0 00 1 -> F1
    assert(buffer[1] == 0xF1);

    CloseBitStream(bs);
}

int main() {
    test_PutBit();
    test_PutBit_EdgeCases();
    test_WriteADTSHeader();
    printf("test_bitstream passed\n");
    return 0;
}
