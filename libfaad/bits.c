/*
 * Bitstream reader implementation using 64-bit Big-Endian Bit-Accumulator
 */

#include "faad_internal.h"

#if defined(__has_builtin)
#if __has_builtin(__builtin_bswap64)
#define HAVE_BSWAP64 1
#endif
#endif

static inline uint64_t load_be64(const uint8_t *p) {
#if defined(HAVE_BSWAP64)
    uint64_t val;
    memcpy(&val, p, 8);
    return __builtin_bswap64(val);
#else
    return ((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48) |
           ((uint64_t)p[2] << 40) | ((uint64_t)p[3] << 32) |
           ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16) |
           ((uint64_t)p[6] << 8)  | (uint64_t)p[7];
#endif
}

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

    /* Fast 64-bit register shift-accumulator path for <= 32 bits */
    if (nbits <= 32 && bs->byte_pos + 8 <= bs->len) {
        uint64_t word = load_be64(bs->buffer + bs->byte_pos);
        uint32_t val = (uint32_t)((word >> (64 - bs->bit_pos - nbits)) & ((1ULL << nbits) - 1ULL));
        uint32_t total_bits = bs->bit_pos + nbits;
        bs->byte_pos += total_bits >> 3;
        bs->bit_pos = total_bits & 7;
        return val;
    }

    /* Fallback byte-by-byte loop near buffer boundary */
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
    if (nbits == 0) return 0;

    if (nbits <= 32 && bs->byte_pos + 8 <= bs->len) {
        uint64_t word = load_be64(bs->buffer + bs->byte_pos);
        return (uint32_t)((word >> (64 - bs->bit_pos - nbits)) & ((1ULL << nbits) - 1ULL));
    }

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

/* Zero-copy memory-mapped slice reader for RFC 3640 RTP AAC Access Units */
void bits_slice_rtp_au(BitReader *sub_bs, const BitReader *parent_bs, uint32_t byte_offset, uint32_t au_len)
{
    if (!sub_bs || !parent_bs) return;
    uint32_t start_byte = parent_bs->byte_pos + byte_offset;
    if (start_byte > parent_bs->len) start_byte = parent_bs->len;
    uint32_t rem_len = parent_bs->len - start_byte;
    if (au_len > rem_len) au_len = rem_len;

    sub_bs->buffer = parent_bs->buffer + start_byte;
    sub_bs->len = au_len;
    sub_bs->byte_pos = 0;
    sub_bs->bit_pos = 0;
}
