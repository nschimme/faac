#include <stdint.h>
#include <string.h>

#include "endian.h"

int main(void)
{
    uint8_t bytes[10] = {0};
    const uint8_t expected[] = {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0};

    if (read_u16_be(expected + 1) != 0x3456 ||
        read_u32_be(expected + 1) != 0x3456789a ||
        read_u64_be(expected) != UINT64_C(0x123456789abcdef0))
        return 1;

    write_u16_be(bytes + 1, 0x3456);
    write_u32_be(bytes + 3, 0x789abcde);
    write_u64_be(bytes + 1, UINT64_C(0x123456789abcdef0));
    if (memcmp(bytes + 1, expected, sizeof(expected)) != 0)
        return 1;

    return 0;
}
