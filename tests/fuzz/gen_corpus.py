#!/usr/bin/env python3
# Writes the seed corpus for the fuzz harnesses into <outdir>/<harness>/.
# Encoder-derived seeds need the faac CLI. Add past crash reproducers below as
# byte literals.
# usage: gen_corpus.py <outdir> <stamp> [faac-binary]
import subprocess
import sys
import tempfile
from pathlib import Path

here = Path(__file__).resolve().parent
out = Path(sys.argv[1])
stamp = Path(sys.argv[2])
faac = sys.argv[3] if len(sys.argv) > 3 else None
max_channels = int(sys.argv[4]) if len(sys.argv) > 4 else 2

names = ['faad_adts', 'faad_raw960', 'faac_encode', 'faac_faad_pce',
         'faam_demux', 'faam_tags', 'faam_roundtrip', 'faam_annexb',
         'faad_mp4', 'frontend_wav', 'frontend_adts_header', 'faar_resampler']
for n in names:
    (out / n).mkdir(parents=True, exist_ok=True)


def put(harness, name, data):
    (out / harness / name).write_bytes(data)


def with_mvex(m4a):
    # A trailing mvex marks the file fragmented; builds without fragment
    # support must reject it cleanly instead of misparsing the rest.
    pos, moov = 0, None
    while pos + 8 <= len(m4a):
        size = int.from_bytes(m4a[pos:pos + 4], 'big')
        if m4a[pos + 4:pos + 8] == b'moov':
            moov = pos
        if size < 8:
            break
        pos += size
    if moov is None or moov + int.from_bytes(m4a[moov:moov + 4], 'big') != len(m4a):
        sys.exit('seed layout changed: moov is not the last box')
    trex = (32).to_bytes(4, 'big') + b'trex' + bytes(4) + (1).to_bytes(4, 'big') + bytes(16)
    mvex = (8 + len(trex)).to_bytes(4, 'big') + b'mvex' + trex
    size = int.from_bytes(m4a[moov:moov + 4], 'big') + len(mvex)
    return m4a[:moov] + size.to_bytes(4, 'big') + m4a[moov + 4:] + mvex


def encode(tmp, name, stdin_len, *args, channels=None):
    path = Path(tmp) / name
    channels = channels or min(2, max_channels)
    r = subprocess.run([faac, '-P', '-C', str(channels), *args, '-o', str(path), '-'],
                       input=bytes(stdin_len), capture_output=True)
    if r.returncode != 0:
        sys.exit('faac failed for %s: %s' % (name, r.stderr.decode(errors='replace')))
    return path.read_bytes()


subprocess.run([sys.executable, str(here / 'seed_faac_encode.py'),
                str(out / 'faac_encode')], check=True)

put('faac_faad_pce', 'seed_8ch', b'\x01\x03' + b'A' * 4096)
put('faac_faad_pce', 'seed_8ch_raw', b'\x05\x03' + b'B' * 4096)
put('faac_faad_pce', 'seed_6ch', b'\x02\x03' + b'C' * 4096)
# Must outgrow the splitter's initial buffer to reach its growth path.
put('faam_annexb', 'large_sample', b'\x00\x00\x00\x01\x65' + b'A' * 200000)
# Minimal 16-bit mono PCM header with an empty data chunk.
put('frontend_wav', 'seed_empty.wav',
    b'RIFF\x24\x00\x00\x00WAVEfmt \x10\x00\x00\x00\x01\x00\x01\x00'
    b'\x40\x1f\x00\x00\x80\x3e\x00\x00\x02\x00\x10\x00data\x00\x00\x00\x00')
put('faar_resampler', 'seed_zeros', bytes(256))

if faac:
    with tempfile.TemporaryDirectory() as tmp:
        m4a = encode(tmp, 's.m4a', 40000, '--title', 'seed')
        adts = encode(tmp, 's.aac', 40000)
        # Seven-plus-one channels exercise the PCE path in encoder and decoder.
        adts8 = m4a8 = None
        if max_channels >= 8:
            adts8 = encode(tmp, 's8.aac', 300000, '-R', '48000', '-a', channels=8)
            m4a8 = encode(tmp, 's8.m4a', 300000, '-R', '48000', channels=8)
        if m4a:
            for h in ('faam_demux', 'faam_roundtrip', 'faad_mp4'):
                put(h, 'seed_m4a', m4a)
            put('faam_tags', 'seed_m4a', b'\x00' + m4a)
            frag = with_mvex(m4a)
            for h in ('faam_demux', 'faad_mp4'):
                put(h, 'seed_mvex_m4a', frag)
            put('faam_tags', 'seed_mvex_m4a', b'\x00' + frag)
        if m4a8:
            put('faad_mp4', 'seed_8ch_m4a', m4a8)
        if adts:
            put('faad_adts', 'seed_adts', b'\x01' + adts)
            put('frontend_adts_header', 'seed_adts', adts[:64])
        if adts8:
            pce = Path(tmp) / 'pce'
            src = Path(tmp) / 's8.aac'
            subprocess.run([sys.executable, str(here / 'seed_faad_pce.py'),
                            str(src), str(pce)], check=True)
            put('faad_adts', 'pce8_adts', (pce / 'pce8_adts').read_bytes())
            put('faad_raw960', 'pce8_raw960',
                (pce / 'pce8_raw960').read_bytes())

# libFuzzer and the replay driver both need a non-empty directory.
for n in names:
    d = out / n
    if not any(d.iterdir()):
        put(n, 'seed_empty', b'\x00')

stamp.write_text('')
