#!/usr/bin/env python3
"""Seed the decoder fuzzers from an ADTS stream that uses a PCE for 7.1."""

import struct
import sys
from pathlib import Path


def frames(blob):
    pos = 0
    while pos + 7 <= len(blob):
        if blob[pos] != 0xFF or (blob[pos + 1] & 0xF0) != 0xF0:
            return
        length = ((blob[pos + 3] & 3) << 11) | (blob[pos + 4] << 3) | (blob[pos + 5] >> 5)
        header = 7 if blob[pos + 1] & 1 else 9
        if length < header or pos + length > len(blob):
            return
        yield blob[pos + header:pos + length]
        pos += length


def main():
    source = Path(sys.argv[1]).read_bytes()
    out = Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)

    units = list(frames(source))
    if not units:
        return 1

    # ADTS input with the harness selector: ADTS, 16-bit output, no downmix.
    (out / "pce8_adts").write_bytes(b"\x00" + source)

    # RAW 960 harness: selector bit 7 means an explicit ASC follows. The ASC is
    # the 11-byte 7.1 PCE form libfaac writes, then length-prefixed access units.
    asc = bytes.fromhex("118004c809000108c80000")
    raw = bytearray(b"\x80" + bytes([len(asc)]) + asc)
    for unit in units[:64]:
        raw += struct.pack("<H", len(unit)) + unit
    (out / "pce8_raw960").write_bytes(raw)
    return 0


if __name__ == "__main__":
    sys.exit(main())
