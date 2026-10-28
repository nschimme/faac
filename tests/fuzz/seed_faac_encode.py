#!/usr/bin/env python3
"""Seed fuzz_faac_encode's parameter header and PCM chunks with valid PS streams."""

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
        # Header format is consumed by fuzz_faac_encode.c: object selector 3 is
        # HE-AAC v2, input selector 1 is 16-bit PCM, rate selector 8 is 48 kHz,
        # rate-control selector 1 is VBR and 2 is CBR.
        flags = (3 << 2) | (1 << 12) | ((2 if cbr else 1) << 17)
        # bit_rate is whole-stream; the non-CBR seed rides VBR with bit_rate 0.
        header = struct.pack(">IBBHHH", flags, 8, 2,
                             0 if not cbr else rate_per_channel // 2,
                             0,
                             100 if not cbr else 0)
        chunks = bytearray()
        for frame in range(8):
            chunks.extend(struct.pack(">H", 4096))
            for i in range(2048):
                sample = int(8000 * math.sin(2 * math.pi * 1000 * (frame * 2048 + i) / 48000))
                chunks.extend(struct.pack("<hh", sample, phase * sample))
        target.write_bytes(header + chunks)

    # Eight channels are the PCE layout: ADTS frames carry the PCE in the first
    # frame and the ASC carries it for raw streams.
    for name, stream_format in (("lc-8ch-adts", 1), ("lc-8ch-raw", 0)):
        target = corpus / name
        if target.exists():
            continue
        flags = (1 << 2) | (stream_format << 10) | (1 << 12) | (1 << 17)
        header = struct.pack(">IBBHHH", flags, 8, 8, 0, 0, 100)
        chunks = bytearray()
        for frame in range(2):
            chunks.extend(struct.pack(">H", 1024 * 8))
            for i in range(1024 * 8):
                sample = int(12000 * math.sin(2 * math.pi * 440 * (frame * 1024 + i) / 48000))
                chunks.extend(struct.pack("<h", sample))
        target.write_bytes(header + chunks)


if __name__ == "__main__":
    main()
