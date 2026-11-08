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
MASK_71_WIDE = 0xFF


def write_wav(path, channels=CHANNELS, tones=TONES, mask=MASK_71):
    frames = RATE * 4
    data = bytearray()
    for i in range(frames):
        data += struct.pack('<%dh' % channels,
                            *[int(5000 * math.sin(2 * math.pi * f * i / RATE)) for f in tones])
    fmt = struct.pack('<HHIIHHHHI16s', 0xFFFE, channels, RATE, RATE * channels * 2, channels * 2, 16,
                      22, 16, mask, bytes.fromhex('0100000000001000800000aa00389b71'))
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


def tone_power(samples, channels, channel, start, count, freq):
    xs = [samples[(start + i) * channels + channel] for i in range(count)]
    re = sum(x * math.cos(2 * math.pi * freq * i / RATE) for i, x in enumerate(xs))
    im = sum(x * math.sin(2 * math.pi * freq * i / RATE) for i, x in enumerate(xs))
    return math.hypot(re, im) / count


def decode(faad, source, out, extra=()):
    r = subprocess.run([faad, *extra, '-o', out, source], capture_output=True)
    assert r.returncode == 0, r.stderr.decode(errors='replace')
    return read_wav(out)


def pce_element(front, side, back, lfe, tag_base=0):
    """A PCE audio element for lists of CPE flags per category, tags counted per type."""
    bits = []

    def put(value, n):
        bits.extend((value >> (n - 1 - i)) & 1 for i in range(n))

    put(5, 3)
    put(0, 4)
    put(1, 2)
    put(3, 4)
    for count in (len(front), len(side), len(back)):
        put(count, 4)
    put(len(lfe), 2)
    put(0, 3)
    put(0, 4)
    put(0, 3)
    next_cpe = tag_base
    for pair in front + side + back:
        put(pair, 1)
        put(next_cpe if pair else 0, 4)
        next_cpe += pair
    for _ in lfe:
        put(0, 4)
    while len(bits) % 8:
        bits.append(0)
    put(0, 8)
    return bytes(sum(b << (7 - i) for i, b in enumerate(bits[j:j + 8])) for j in range(0, len(bits), 8))


def config0_stream(frames, pce, every):
    """The frames with channel configuration 0 and the PCE in every `every`th."""
    out = bytearray()
    for i, frame in enumerate(frames):
        header = bytearray(frame[:7])
        header[2] &= 0xFE
        header[3] &= 0x3F
        payload = (pce if every and i % every == 0 else b'') + frame[7:]
        length = 7 + len(payload)
        header[3] = (header[3] & 0xFC) | (length >> 11)
        header[4] = (length >> 3) & 0xFF
        header[5] = ((length & 7) << 5) | (header[5] & 0x1F)
        out += header + payload
    return bytes(out)


def as_config7(frames):
    """FAAC's 7.1 frames with the PCE dropped and channel configuration 7 in the header."""
    pce = pce_element([0, 1], [1], [1], [0])
    out = bytearray()
    for frame in frames:
        payload = frame[7:]
        if payload.startswith(pce):
            payload = payload[len(pce):]
        header = bytearray(frame[:7])
        header[2] |= 1
        header[3] |= 0xC0
        length = 7 + len(payload)
        header[3] = (header[3] & 0xFC) | (length >> 11)
        header[4] = (length >> 3) & 0xFF
        header[5] = ((length & 7) << 5) | (header[5] & 0x1F)
        out += header + payload
    return bytes(out)


def check_switch(faad, d, frames):
    """A PCE stream that follows a configuration 7 stream is still read by its PCE."""
    plain = as_config7(frames)
    for name, tail in (('whole', frames), ('cut', frames[5:])):
        path = os.path.join(d, 'switch_%s.aac' % name)
        open(path, 'wb').write(plain + b''.join(tail))
        channels, mask, samples = decode(faad, path, path + '.wav')
        # The format is the one the configuration 7 stream opened with, so the
        # side/back segment that follows is written under the wide mask.
        assert (channels, mask) == (CHANNELS, MASK_71_WIDE), (name, channels, hex(mask))
        total = len(samples) // channels
        for ch in range(CHANNELS):
            assert tone_of(samples, channels, ch, total - 8192, 2048) == ch, ('switch', name, ch)


def check_config7_wide(faad, d, frames):
    """Configuration 7 is 7.1 wide: the pair after the centre is FLC/FRC, then FL/FR, then the surrounds."""
    path = os.path.join(d, 'config7.aac')
    open(path, 'wb').write(as_config7(frames))
    channels, mask, samples = decode(faad, path, path + '.wav')
    assert (channels, mask) == (CHANNELS, MASK_71_WIDE), (channels, hex(mask))
    # Output FL FR FC LFE BL BR FLC FRC carries the stream's pairs 2, 2, C, LFE, 3, 3, 1, 1.
    total = len(samples) // channels
    for ch, tone in enumerate((6, 7, 2, 3, 4, 5, 0, 1)):
        assert tone_of(samples, channels, ch, total - 8192, 2048) == tone, ('config7', ch)
    # The inner front pair is part of the stereo downmix, at the weight of the centre.
    channels, _, samples = decode(faad, path, path + '.dm.wav', ['-d', 'stereo'])
    total = len(samples) // channels
    for ch, (inner, front) in enumerate(((TONES[0], TONES[6]), (TONES[1], TONES[7]))):
        ratio = tone_power(samples, channels, ch, total - 8192, 4096, inner) / \
            tone_power(samples, channels, ch, total - 8192, 4096, front)
        assert 0.5 < ratio < 0.9, ('config7 downmix', ch, ratio)


def check_layouts(faac, faad, d):
    """Other PCE layouts: order, mask, a join without the PCE, and the downmix."""
    src = os.path.join(d, 'in6.wav')
    write_wav(src, 6, TONES[:6], 0x3F)
    ref = os.path.join(d, 'ref.aac')
    r = subprocess.run([faac, '--profile', 'lc', '-b', '320', '--stream-format', 'adts', '-o', ref, src],
                       capture_output=True)
    assert r.returncode == 0, r.stderr.decode(errors='replace')
    frames = adts_frames(open(ref, 'rb').read())
    ref_dm = decode(faad, ref, ref + '.wav', ['-d', 'stereo'])
    # (front, side, back, lfe) in list order and the mask a full PCE gives.
    for name, lists, mask in (('back', ([0, 1], [], [1], [0]), 0x3F), ('side', ([0, 1], [1], [], [0]), 0x60F)):
        pce = pce_element(*lists)
        full = os.path.join(d, name + '.aac')
        open(full, 'wb').write(config0_stream(frames, pce, 32))
        channels, got, samples = decode(faad, full, full + '.wav')
        assert (channels, got) == (6, mask), (name, channels, hex(got))
        total = len(samples) // channels
        for ch in range(6):
            assert tone_of(samples, channels, ch, total // 2, 2048) == ch, (name, ch)
        assert decode(faad, full, full + '.dm.wav', ['-d', 'stereo']) == ref_dm, name
        # A PCE whose tags the stream does not use still places the channels.
        odd = os.path.join(d, name + '.tags.aac')
        open(odd, 'wb').write(config0_stream(frames, pce_element(*lists, tag_base=5), 32))
        channels, got, samples = decode(faad, odd, odd + '.wav')
        assert (channels, got) == (6, mask), (name, 'tags', channels, hex(got))
        total = len(samples) // channels
        for ch in range(6):
            assert tone_of(samples, channels, ch, total // 2, 2048) == ch, (name, 'tags', ch)
        # Joined with no PCE at all: the elements alone give the order.
        cut = os.path.join(d, name + '.cut.aac')
        open(cut, 'wb').write(config0_stream(frames[5:], pce, 0))
        channels, _, samples = decode(faad, cut, cut + '.wav')
        total = len(samples) // channels
        assert channels == 6
        for ch in range(6):
            assert tone_of(samples, channels, ch, total // 2, 2048) == ch, (name, 'cut', ch)


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
        check_switch(faad, d, frames)
        check_config7_wide(faad, d, frames)
        check_layouts(faac, faad, d)
    print('ok')


if __name__ == '__main__':
    main()
