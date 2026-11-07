#!/usr/bin/env python3
# FAAC - Freeware Advanced Audio Coder
# Copyright (C) 2026 Nils Schimmelmann
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.

"""Surround streams from FFmpeg, Apple and fdkaac decode in WAVE order with the right mask.

Usage: test_faad_encoder_streams.py <faad> <data-directory>
"""
import math
import os
import struct
import subprocess
import sys
import tempfile

RATE = 48000
TONES = [440, 660, 880, 60, 1320, 1760, 2200, 2640]

# file: (channels, WAVE channel mask, channels to check)
STREAMS = {
    'ffmpeg_51.aac': (6, 0x3F, 6),
    'ffmpeg_61.m4a': (7, 0x13F, 7),
    'ffmpeg_70.m4a': (7, 0x637, 7),
    'ffmpeg_71.m4a': (8, 0x63F, 8),
    'apple_61.m4a': (7, 0x13F, 4),
    'apple_70.m4a': (7, 0x637, 7),
    'fdkaac_71.aac': (8, 0x63F, 8),
}


def read_wav(path):
    data = open(path, 'rb').read()
    pos = data.find(b'fmt ')
    channels = struct.unpack_from('<H', data, pos + 10)[0]
    mask = struct.unpack_from('<I', data, pos + 28)[0]
    start = data.find(b'data') + 8
    return channels, mask, struct.unpack_from('<%dh' % ((len(data) - start) // 2), data, start)


def tone_of(samples, channels, channel, start, count):
    best, best_power = None, -1.0
    xs = [samples[(start + i) * channels + channel] for i in range(count)]
    for k, freq in enumerate(TONES):
        re = sum(x * math.cos(2 * math.pi * freq * i / RATE) for i, x in enumerate(xs))
        im = sum(x * math.sin(2 * math.pi * freq * i / RATE) for i, x in enumerate(xs))
        if re * re + im * im > best_power:
            best, best_power = k, re * re + im * im
    return best


def main():
    faad, data = sys.argv[1:3]
    with tempfile.TemporaryDirectory() as d:
        for name, (channels, mask, check) in STREAMS.items():
            out = os.path.join(d, name + '.wav')
            r = subprocess.run([faad, '-o', out, os.path.join(data, name)], capture_output=True)
            assert r.returncode == 0, (name, r.stderr.decode(errors='replace'))
            got_channels, got_mask, samples = read_wav(out)
            assert (got_channels, got_mask) == (channels, mask), (name, got_channels, hex(got_mask))
            total = len(samples) // channels
            for start in (3000, total // 2):
                for ch in range(check):
                    assert tone_of(samples, channels, ch, start, 1024) == ch, (name, start, ch)
    print('ok')


if __name__ == '__main__':
    main()
