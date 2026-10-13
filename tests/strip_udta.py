#!/usr/bin/env python3
"""strip_udta.py in out: drop moov/udta (and any stco shift for moov-first files) to model muxers that write none."""
import struct, sys
b = bytearray(open(sys.argv[1], 'rb').read())
def box(b, s, e, name):
    while s + 8 <= e:
        sz, t = struct.unpack_from('>I4s', b, s)
        if t == name: return s, sz
        s += sz
moov = box(b, 0, len(b), b'moov'); mdat = box(b, 0, len(b), b'mdat')
udta = box(b, moov[0] + 8, moov[0] + moov[1], b'udta')
if udta:
    del b[udta[0]:udta[0] + udta[1]]
    struct.pack_into('>I', b, moov[0], moov[1] - udta[1])
    if mdat[0] > moov[0]:
        i = b.find(b'stco')
        while i >= 0:
            n, = struct.unpack_from('>I', b, i + 8)
            for k in range(n):
                v, = struct.unpack_from('>I', b, i + 12 + 4 * k)
                struct.pack_into('>I', b, i + 12 + 4 * k, v - udta[1])
            i = b.find(b'stco', i + 4)
open(sys.argv[2], 'wb').write(b)
