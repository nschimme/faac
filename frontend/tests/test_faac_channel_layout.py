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

"""WAVE channel mask warning and the faad -> faac layout round trip.

Usage: test_faac_channel_layout.py <faac> <faad> <max-channels>
"""
import os
import struct
import subprocess
import sys
import tempfile

WARNING = 'the WAVE channel mask'
PCM_GUID = bytes.fromhex('0100000000001000800000aa00389b71')

# (channels, mask) pairs FAAC reads without comment, and ones it warns about.
QUIET = [(3, 0x7), (4, 0x107), (5, 0x37), (5, 0x607), (6, 0x3F), (6, 0x60F), (8, 0x63F), (6, 0)]
LOUD = [(4, 0x33), (6, 0x137), (3, 0xB)]


def write_wav(path, channels, mask, rate=48000, frames=4800):
    data = b''.join(struct.pack('<%dh' % channels, *[(i * 7 + c * 1000) % 8000 - 4000
                                                     for c in range(channels)])
                    for i in range(frames))
    if mask:
        fmt = struct.pack('<HHIIHHHHI16s', 0xFFFE, channels, rate, rate * channels * 2,
                          channels * 2, 16, 22, 16, mask, PCM_GUID)
    else:
        fmt = struct.pack('<HHIIHH', 1, channels, rate, rate * channels * 2, channels * 2, 16)
    with open(path, 'wb') as f:
        f.write(b'RIFF' + struct.pack('<I', 4 + 8 + len(fmt) + 8 + len(data)) + b'WAVEfmt ')
        f.write(struct.pack('<I', len(fmt)) + fmt + b'data' + struct.pack('<I', len(data)) + data)


def encode(faac, src, dst, *extra):
    r = subprocess.run([faac, '-b', '128', *extra, '-o', dst, src], capture_output=True)
    return r.returncode, r.stderr.decode(errors='replace')


def mask_of(path):
    data = open(path, 'rb').read()
    pos = data.find(b'fmt ')
    tag = struct.unpack_from('<H', data, pos + 8)[0]
    return struct.unpack_from('<I', data, pos + 8 + 20)[0] if tag == 0xFFFE else 0


def main():
    faac, faad = sys.argv[1:3]
    max_channels = int(sys.argv[3])
    with tempfile.TemporaryDirectory() as d:
        for channels, mask in QUIET + LOUD:
            if channels > max_channels:
                continue
            src = os.path.join(d, 'in-%d-%x.wav' % (channels, mask))
            write_wav(src, channels, mask)
            rc, err = encode(faac, src, src + '.m4a')
            assert rc == 0, err
            has = WARNING in err
            assert has == ((channels, mask) in LOUD), (channels, hex(mask), err)
            if (channels, mask) in LOUD and channels >= 6:
                assert 'LFE' in err, err
            if (channels, mask) in LOUD and channels >= 6:
                # A non-default -I means the caller chose the order.
                rc, err = encode(faac, src, src + '.i.m4a', '-I', '2,5')
                assert rc == 0 and WARNING not in err, err

        # Whatever faad writes for a layout, faac must read back without a warning.
        for channels in (3, 4, 5, 6, 8):
            if channels > max_channels:
                continue
            mask = {3: 0x7, 4: 0x107, 5: 0x37, 6: 0x3F, 8: 0x63F}[channels]
            src = os.path.join(d, 'rt-%d.wav' % channels)
            write_wav(src, channels, mask)
            rc, err = encode(faac, src, src + '.aac', '-a')
            assert rc == 0, err
            out = src + '.dec.wav'
            r = subprocess.run([faad, '-o', out, src + '.aac'], capture_output=True)
            assert r.returncode == 0, r.stderr.decode(errors='replace')
            assert mask_of(out) == mask, (channels, hex(mask_of(out)))
            rc, err = encode(faac, out, out + '.m4a')
            assert rc == 0 and WARNING not in err, err

        if max_channels >= 7:
            src = os.path.join(d, 'seven.wav')
            write_wav(src, 7, 0x637)
            rc, err = encode(faac, src, src + '.m4a')
            assert rc != 0 and '7 channels' in err, err
    print('ok')


if __name__ == '__main__':
    main()
