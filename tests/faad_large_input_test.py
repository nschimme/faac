"""Streaming boundaries and sparse compressed input beyond 4 GiB."""
import json
import os
import struct
import subprocess
import sys
import tempfile
import wave

faac, faad, max_channels = sys.argv[1:]
channels = min(2, int(max_channels))


def run(args, **kw):
    return subprocess.run(args, check=True, capture_output=True, **kw).stdout


def boxes(buf, start=0, end=None):
    end = len(buf) if end is None else end
    while start + 8 <= end:
        size, kind = struct.unpack_from('>I4s', buf, start)
        head = 8
        if size == 1:
            size = struct.unpack_from('>Q', buf, start + 8)[0]
            head = 16
        assert size >= head and start + size <= end
        yield kind, start, start + head, start + size
        start += size


with tempfile.TemporaryDirectory(prefix='faad-large-input-') as d:
    wav = os.path.join(d, 'source.wav')
    with wave.open(wav, 'wb') as f:
        f.setparams((channels, 2, 48000, 0, 'NONE', ''))
        f.writeframes(bytes(2 * channels * 8192))
    m4a = os.path.join(d, 'source.m4a')
    aac = os.path.join(d, 'source.aac')
    run([faac, '--object-type=lc', '-o', m4a, wav])
    run([faad, '-a', aac, m4a])
    ref = run([faad, '-q', '-w', '-f', 'raw', aac])
    compressed = open(aac, 'rb').read()
    # Syncwords and frame payloads straddle each refill point. Prefix garbage
    # makes the valid stream land at every relevant boundary around 256 KiB.
    for shift in (0, 1, 2, 6, 8, 8190):
        payload = b'\0' * (256 * 1024 - shift) + compressed
        assert run([faad, '-q', '-f', 'raw', '-w', '-'], input=payload) == ref
    mp4_ref = run([faad, '-q', '-w', '-f', 'raw', m4a])
    assert run([faad, '-q', '-w', '-f', 'raw', '-'], input=open(m4a, 'rb').read()) == mp4_ref
    # Streaming output must never truncate its own input, including hardlinks.
    alias = os.path.join(d, 'alias.m4a')
    os.link(m4a, alias)
    for option in ('-o', '-a'):
        p = subprocess.run([faad, '--overwrite', option, alias, m4a], capture_output=True)
        assert p.returncode != 0 and b'different files' in p.stderr
    if os.name != 'nt':
        big = (1 << 32) + 8191
        sparse = os.path.join(d, 'large.aac')
        with open(sparse, 'wb') as f:
            f.seek(big)
            f.write(compressed)
        assert run([faad, '-q', '-w', '-f', 'raw', sparse]) == ref
        info = json.loads(run([faad, '--json', sparse]))
        small_info = json.loads(run([faad, '--json', aac]))
        assert info['audio']['total_frames'] == small_info['audio']['total_frames']
        src = open(m4a, 'rb').read()
        top = {kind: (start, end) for kind, start, _, end in boxes(src)}
        ftyp = src[slice(*top[b'ftyp'])]
        moov = src[slice(*top[b'moov'])]
        media = src[top[b'ftyp'][1]:top[b'mdat'][1]]

        def rebuild(buf, a, b, shift):
            out = b''
            for kind, start, body, end in boxes(buf, a, b):
                if kind in (b'trak', b'mdia', b'minf', b'stbl'):
                    inner = rebuild(buf, body, end, shift)
                    out += struct.pack('>I4s', 8 + len(inner), kind) + inner
                elif kind == b'stco':
                    count = struct.unpack_from('>I', buf, body + 4)[0]
                    inner = buf[body:body+8] + b''.join(struct.pack('>Q',
                        struct.unpack_from('>I', buf, body + 8 + 4*i)[0] + shift) for i in range(count))
                    out += struct.pack('>I4s', 8 + len(inner), b'co64') + inner
                else:
                    out += buf[start:end]
            return out

        for first in (False, True):
            inner = rebuild(moov, 8, len(moov), 0)
            shift = big + (8 + len(inner) if first else 0)
            inner = rebuild(moov, 8, len(moov), shift)
            mv = struct.pack('>I4s', len(inner)+8, b'moov') + inner
            sparse = os.path.join(d, 'large.m4a')
            with open(sparse, 'wb') as f:
                f.write(ftyp)
                if first: f.write(mv)
                f.write(struct.pack('>I4sQ', 1, b'free', big))
                f.seek(big-16, 1)
                f.write(media)
                if not first: f.write(mv)
            assert run([faad, '-q', '-w', '-f', 'raw', sparse]) == mp4_ref
            extracted = os.path.join(d, 'extracted.aac')
            run([faad, '--overwrite', '-a', extracted, sparse])
            assert open(extracted, 'rb').read() == compressed
print('large streaming inputs: ok')
