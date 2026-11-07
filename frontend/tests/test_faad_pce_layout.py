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

"""A 7.1 ADTS stream keeps its channel order and mask however it is joined.

Usage: test_faad_pce_layout.py <faac> <faad>
"""
import math
import os
import struct
import subprocess
import sys
import tempfile

RATE = 48000
CHANNELS = 8
# Distinct tone per input channel, in WAVE order L R C LFE BL BR SL SR.
TONES = [440, 660, 880, 110, 1320, 1760, 2200, 2640]
MASK_71 = 0x63F


def write_wav(path):
    frames = RATE * 4
    data = bytearray()
    for i in range(frames):
        data += struct.pack('<%dh' % CHANNELS,
                            *[int(5000 * math.sin(2 * math.pi * f * i / RATE)) for f in TONES])
    fmt = struct.pack('<HHIIHHHHI16s', 0xFFFE, CHANNELS, RATE, RATE * CHANNELS * 2, CHANNELS * 2, 16,
                      22, 16, MASK_71, bytes.fromhex('0100000000001000800000aa00389b71'))
    with open(path, 'wb') as f:
        f.write(b'RIFF' + struct.pack('<I', 4 + 8 + len(fmt) + 8 + len(data)) + b'WAVEfmt ')
        f.write(struct.pack('<I', len(fmt)) + fmt + b'data' + struct.pack('<I', len(data)) + bytes(data))


def adts_frames(data):
    frames, pos = [], 0
    while pos + 7 <= len(data):
        length = ((data[pos + 3] & 3) << 11) | (data[pos + 4] << 3) | (data[pos + 5] >> 5)
        frames.append(data[pos:pos + length])
        pos += length
    return frames


def read_wav(path):
    data = open(path, 'rb').read()
    pos = data.find(b'fmt ')
    channels = struct.unpack_from('<H', data, pos + 10)[0]
    mask = struct.unpack_from('<I', data, pos + 28)[0] if struct.unpack_from('<H', data, pos + 8)[0] == 0xFFFE else 0
    start = data.find(b'data') + 8
    samples = struct.unpack_from('<%dh' % ((len(data) - start) // 2), data, start)
    return channels, mask, samples


def tone_of(samples, channels, channel, start, count):
    """Index of the TONES entry with the most energy in the channel."""
    best, best_power = None, -1.0
    xs = [samples[(start + i) * channels + channel] for i in range(count)]
    for k, freq in enumerate(TONES):
        re = sum(x * math.cos(2 * math.pi * freq * i / RATE) for i, x in enumerate(xs))
        im = sum(x * math.sin(2 * math.pi * freq * i / RATE) for i, x in enumerate(xs))
        if re * re + im * im > best_power:
            best, best_power = k, re * re + im * im
    return best


def decode(faad, source, out):
    r = subprocess.run([faad, '-o', out, source], capture_output=True)
    assert r.returncode == 0, r.stderr.decode(errors='replace')
    return read_wav(out)


def main():
    faac, faad = sys.argv[1:3]
    with tempfile.TemporaryDirectory() as d:
        src = os.path.join(d, 'in.wav')
        write_wav(src)
        full = os.path.join(d, 'full.aac')
        r = subprocess.run([faac, '--profile', 'lc', '-b', '448', '--stream-format', 'adts', '-o', full, src],
                           capture_output=True)
        assert r.returncode == 0, r.stderr.decode(errors='replace')
        frames = adts_frames(open(full, 'rb').read())
        # FAAC repeats the PCE every 32 frames, so these joins have no PCE for a while.
        for skip in (0, 5, 33):
            cut = os.path.join(d, 'cut%d.aac' % skip)
            open(cut, 'wb').write(b''.join(frames[skip:]))
            channels, mask, samples = decode(faad, cut, cut + '.wav')
            assert (channels, mask) == (CHANNELS, MASK_71), (skip, channels, hex(mask))
            total = len(samples) // channels
            # Check the start, the middle and the end, across the 32 frame PCE period.
            for start in (4096, total // 2, total - 8192):
                for ch in range(CHANNELS):
                    assert tone_of(samples, channels, ch, start, 2048) == ch, (skip, start, ch)
    print('ok')


if __name__ == '__main__':
    main()
