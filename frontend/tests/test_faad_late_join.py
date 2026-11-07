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

"""An HE-AAC ADTS stream joined between SBR headers keeps one output format,
and a joined HE-AAC v2 stream is stereo from its first sample.

Usage: test_faad_late_join.py <faac> <faad> <ps: true|false>
"""
import math
import os
import struct
import subprocess
import sys
import tempfile
import wave


def write_wav(path, rate, seconds):
    with wave.open(path, 'wb') as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(b''.join(
            struct.pack('<hh', int(6000 * math.sin(2 * math.pi * 440 * i / rate)),
                        int(6000 * math.sin(2 * math.pi * 660 * i / rate)))
            for i in range(rate * seconds)))


def adts_frames(data):
    frames, pos = [], 0
    while pos + 7 <= len(data):
        length = ((data[pos + 3] & 3) << 11) | (data[pos + 4] << 3) | (data[pos + 5] >> 5)
        frames.append(data[pos:pos + length])
        pos += length
    return frames


def wav_info(path):
    with wave.open(path, 'rb') as w:
        return w.getframerate(), w.getnchannels(), w.getnframes()


def main():
    faac, faad = sys.argv[1:3]
    with_ps = sys.argv[3] == 'true'
    with tempfile.TemporaryDirectory() as d:
        src = os.path.join(d, 'in.wav')
        write_wav(src, 48000, 4)
        full = os.path.join(d, 'full.aac')
        r = subprocess.run([faac, '--profile', 'he-v1', '-b', '48', '--stream-format', 'adts',
                            '-o', full, src], capture_output=True)
        assert r.returncode == 0, r.stderr.decode(errors='replace')
        frames = adts_frames(open(full, 'rb').read())

        ref = os.path.join(d, 'full.wav')
        r = subprocess.run([faad, '-o', ref, full], capture_output=True)
        assert r.returncode == 0, r.stderr.decode(errors='replace')
        rate, channels, _ = wav_info(ref)
        assert rate == 48000, rate

        # Frames 1 and 5 carry no SBR header; the next header is about 30 frames later.
        for skip in (1, 5, 17):
            cut = os.path.join(d, 'cut%d.aac' % skip)
            open(cut, 'wb').write(b''.join(frames[skip:]))
            out = cut + '.wav'
            r = subprocess.run([faad, '-o', out, cut], capture_output=True)
            assert r.returncode == 0, (skip, r.stderr.decode(errors='replace'))
            got = wav_info(out)
            assert got[:2] == (rate, channels), (skip, got)
            assert got[2] >= (len(frames) - skip - 1) * 2048, (skip, got)
        if with_ps:
            ps = os.path.join(d, 'ps.aac')
            r = subprocess.run([faac, '--profile', 'he-v2', '-b', '24', '--stream-format', 'adts',
                                '-o', ps, src], capture_output=True)
            assert r.returncode == 0, r.stderr.decode(errors='replace')
            ps_frames = adts_frames(open(ps, 'rb').read())
            for skip in (1, 5):
                cut = os.path.join(d, 'ps%d.aac' % skip)
                open(cut, 'wb').write(b''.join(ps_frames[skip:]))
                out = cut + '.wav'
                r = subprocess.run([faad, '-o', out, cut], capture_output=True)
                assert r.returncode == 0, (skip, r.stderr.decode(errors='replace'))
                got = wav_info(out)
                assert got[:2] == (48000, 2), (skip, got)
                assert got[2] >= (len(ps_frames) - skip - 1) * 2048, (skip, got)
    print('ok')


if __name__ == '__main__':
    main()
