#!/usr/bin/env python3
"""Convert the in-tree little-endian DAB vectors to the RAW960 fuzz layout."""
from pathlib import Path
import struct


def main():
    root = Path(__file__).resolve().parents[2]
    out = root / 'tests/fuzz/corpus_raw960'
    out.mkdir(exist_ok=True)
    selectors = {'lc_mono32': 0, 'lc_st48': 1, 'he2_st48': 3}
    for name, selector in selectors.items():
        data = (root / 'tests/data/dab' / (name + '.dab')).read_bytes()
        pos = 0

        def field():
            nonlocal pos
            if len(data) - pos < 4:
                raise ValueError(f'{name}: truncated length')
            length, = struct.unpack_from('<I', data, pos)
            pos += 4
            if length > len(data) - pos:
                raise ValueError(f'{name}: truncated field')
            value = data[pos:pos + length]
            pos += length
            return value

        asc = field()
        if not 0 < len(asc) <= 255:
            raise ValueError(f'{name}: ASC too long or empty')
        units = bytearray()
        count = 0
        while pos < len(data) and count < 16:
            unit = field()
            if len(unit) > 65535:
                raise ValueError(f'{name}: AU too long')
            units += struct.pack('<H', len(unit)) + unit
            count += 1
        if count < 8:
            raise ValueError(f'{name}: fewer than 8 access units')
        raw_asc = bytes([0x80, len(asc)]) + asc
        (out / (name + '_canned')).write_bytes(bytes([selector]) + units)
        (out / (name + '_asc')).write_bytes(raw_asc + units)
        (out / (name + '_asc_only')).write_bytes(raw_asc)
        print(f'{name}: {count} access units, ASC {asc.hex()}')


if __name__ == '__main__':
    main()
