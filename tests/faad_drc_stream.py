"""Bit-level DRC payload and ADTS splicing helpers; no external decoder oracle."""

def bits(value, width):
    assert 0 <= value < 1 << width
    return f"{value:0{width}b}"


def drc(sign=1, ctl=24, ref=None, tops=(), controls=(), exclude=False):
    payload = '1011' + '0' + bits(int(exclude), 1)
    if exclude:
        payload += '01000000'  # FL stays active; FR is excluded.
    payload += bits(bool(tops), 1)
    if tops:
        payload += bits(len(tops) - 1, 4) + '0000'
        payload += ''.join(bits(t, 8) for t in tops)
    payload += bits(ref is not None, 1)
    if ref is not None:
        payload += bits(ref, 7) + '0'
    payload += ''.join(bits(sign, 1) + bits(c, 7) for c in (controls or (ctl,)))
    return payload + '0' * (-len(payload) % 8)


def inject(data, payload, before=True, count=None):
    out = bytearray()
    pos = 0
    while pos < len(data):
        h = bytearray(data[pos:pos + 7])
        n = ((h[3] & 3) << 11) | h[4] << 3 | h[5] >> 5
        assert h[1] & 1 and n >= 7
        raw = ''.join(bits(b, 8) for b in data[pos + 7:pos + n])
        end = len(raw.rstrip('0')) - 3
        assert raw[end:end + 3] == '111'
        size = len(payload) // 8 if count is None else count
        fill = '110' + bits(min(size, 15), 4)
        if size >= 15:
            fill += bits(size - 14, 8)
        fill += payload
        raw = fill + raw[:end + 3] if before else raw[:end] + fill + '111'
        raw += '0' * (-len(raw) % 8)
        body = bytes(int(raw[i:i + 8], 2) for i in range(0, len(raw), 8))
        length = 7 + len(body)
        h[3] = (h[3] & 252) | length >> 11
        h[4] = (length >> 3) & 255
        h[5] = (h[5] & 31) | (length & 7) << 5
        out += h + body
        pos += n
    assert pos == len(data)
    return out


