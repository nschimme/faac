/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2026 Nils Schimmelmann
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include "coder.h"
#include "bitstream.h"

void test_PutBit() {
    unsigned char buffer[100];
    memset(buffer, 0, 100);
    BitStream *bs = OpenBitStream(100, buffer);
    assert(bs != NULL);

    /* Verify bit-level packing and offset logic */
    PutBit(bs, 0xAA, 8);
    assert(buffer[0] == 0xAA);
    assert(bs->numBit == 8);

    /* Partial byte packing */
    PutBit(bs, 0x5, 4);
    assert(buffer[1] == 0x50);
    assert(bs->numBit == 12);

    /* Byte boundary crossing */
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

    /* Null writes */
    PutBit(bs, 0x1234, 0);
    assert(bs->numBit == 0);

    /* Word-sized atomic writes (validating mask logic) */
    PutBit(bs, 0xDEADBEEF, 32);
    assert(bs->numBit == 32);
    assert(buffer[0] == 0xDE);
    assert(buffer[1] == 0xAD);
    assert(buffer[2] == 0xBE);
    assert(buffer[3] == 0xEF);

    /* Verify multi-byte packing with non-aligned start */
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
    encoder.config.mpegVersion = 0; /* MPEG-4 */
    encoder.config.aacObjectType = 2; /* Low Complexity (LC) */
    encoder.sampleRateIdx = 4; /* 44100 Hz */
    encoder.numChannels = 2;
    encoder.usedBytes = 100;

    WriteADTSHeader(&encoder, bs, 1);

    /* ADTS Syncword: 0xFFF (12 bits) */
    assert(buffer[0] == 0xFF);
    /* ID, Layer, protection_absent */
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
