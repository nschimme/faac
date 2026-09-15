/*
 * Bitstream reader implementation
 */

#include "faad_internal.h"

void bits_init(BitReader *bs, const uint8_t *buffer, uint32_t len)
{
    bs->buffer = buffer;
    bs->len = len;
    bs->byte_pos = 0;
    bs->bit_pos = 0;
}

uint32_t bits_get(BitReader *bs, uint32_t nbits)
{
    if (nbits == 0) return 0;
    uint32_t val = 0;
    while (nbits > 0) {
        if (bs->byte_pos >= bs->len) {
            return val << nbits;
        }
        uint32_t bits_left_in_byte = 8 - bs->bit_pos;
        if (nbits <= bits_left_in_byte) {
            uint32_t mask = (1U << nbits) - 1U;
            val = (val << nbits) | ((bs->buffer[bs->byte_pos] >> (bits_left_in_byte - nbits)) & mask);
            bs->bit_pos += nbits;
            if (bs->bit_pos == 8) {
                bs->bit_pos = 0;
                bs->byte_pos++;
            }
            break;
        } else {
            val = (val << bits_left_in_byte) | (bs->buffer[bs->byte_pos] & ((1U << bits_left_in_byte) - 1U));
            nbits -= bits_left_in_byte;
            bs->bit_pos = 0;
            bs->byte_pos++;
        }
    }
    return val;
}

uint32_t bits_show(BitReader *bs, uint32_t nbits)
{
    BitReader tmp = *bs;
    return bits_get(&tmp, nbits);
}

void bits_skip(BitReader *bs, uint32_t nbits)
{
    uint32_t total_bits = bs->byte_pos * 8 + bs->bit_pos + nbits;
    bs->byte_pos = total_bits / 8;
    bs->bit_pos = total_bits % 8;
    if (bs->byte_pos > bs->len) {
        bs->byte_pos = bs->len;
        bs->bit_pos = 0;
    }
}

void bits_byte_align(BitReader *bs)
{
    if (bs->bit_pos != 0) {
        bs->bit_pos = 0;
        bs->byte_pos++;
    }
}

uint32_t bits_get_consumed(BitReader *bs)
{
    return bs->byte_pos * 8 + bs->bit_pos;
}
