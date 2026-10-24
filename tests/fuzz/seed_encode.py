#!/usr/bin/env python3
"""Seed fuzz_encode's parameter header and PCM chunks with valid PS streams."""

import math
from pathlib import Path
import struct
import sys


def main():
    corpus = Path(sys.argv[1])
    # Stereo 48 kHz, 16-bit PCM: correlated and antiphase material exercise
    # both the ordinary downmix and the spectral carrier.
    for name, rate_per_channel, cbr, phase in (
        ("ps-low", 6000, False, 1),
        ("ps-cbr", 8000, True, -1),
    ):
        target = corpus / name
        if target.exists():
            continue
        # Header format is consumed by fuzz_encode.c: object selector 3 is
        # HE-AAC v2, input selector 1 is 16-bit PCM, rate selector 8 is 48 kHz.
        flags = (3 << 2) | (1 << 12) | ((3 if cbr else 0) << 17)
        header = struct.pack(">IBBHHH", flags, 8, 2, rate_per_channel // 4, 0, 100)
        chunks = bytearray()
        for frame in range(8):
            chunks.extend(struct.pack(">H", 4096))
            for i in range(2048):
                sample = int(8000 * math.sin(2 * math.pi * 1000 * (frame * 2048 + i) / 48000))
                chunks.extend(struct.pack("<hh", sample, phase * sample))
        target.write_bytes(header + chunks)


if __name__ == "__main__":
    main()
