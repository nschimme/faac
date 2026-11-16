#!/usr/bin/env python3
"""Corpus hash check vs apple-48k manifest (src_sha256). Never modifies source WAVs."""
import argparse, hashlib, json, math, os, struct, wave
import numpy as np

MAN = '/home/user/faac/probe/ladder/ref/apple-48k/manifest.json'
OUT = '/home/user/faac/probe/ladder/results/s12-apple48k/stage0'
KNOWN = ['62.12.100']  # tried first (older fixer)

def sha(b): return hashlib.sha256(b).hexdigest()

def swap_isft(b, ver):
    """Rewrite RIFF LIST/INFO/ISFT to Lavf<ver>; returns bytes or None if no LIST."""
    i = b.find(b'LIST'); j = b.find(b'data')
    if i < 0 or j < i: return None
    s = b'Lavf' + ver.encode() + b'\0'
    s += b'\0' * (len(s) % 2)
    info = b'INFO' + b'ISFT' + struct.pack('<I', len(b'Lavf' + ver.encode()) + 1) + s
    out = b[:i] + b'LIST' + struct.pack('<I', len(info)) + info + b[j:]
    return out[:4] + struct.pack('<I', len(out) - 8) + out[8:]

def cands():
    for v in KNOWN: yield v
    for a in range(60, 66):
        for b in range(0, 40):
            for c in (100, 101):
                v = f'{a}.{b}.{c}'
                if v not in KNOWN: yield v

def info(path):
    with wave.open(path) as w:
        n, sr, ch, sw = w.getnframes(), w.getframerate(), w.getnchannels(), w.getsampwidth()
        x = np.frombuffer(w.readframes(n), dtype='<i2').astype(np.float64) / 32768
    rms = math.sqrt(float(np.mean(x * x))) if len(x) else 0
    return dict(frames=n, rate=sr, channels=ch, duration_s=n / sr,
                rms_dbfs=20 * math.log10(rms) if rms > 0 else None,
                peak=float(np.max(np.abs(x))) if len(x) else 0)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--audio', default='/home/user/work/fb/data/external/audio')
    ap.add_argument('--norm-dir', default='/home/user/work/audio_norm')
    ap.add_argument('--out', default=OUT)
    a = ap.parse_args()
    m = json.load(open(MAN))
    exp, mdur = {}, {}
    for r in m['rows']:
        exp[r['clip']] = r['src_sha256']
        if r['profile'] == 'lc': mdur.setdefault(r['clip'], set()).add(round(r['duration_s'], 6))
    good = []  # ISFT versions that matched so far (tried first)
    rows = []
    for clip in sorted(exp):
        p = os.path.join(a.audio, clip)
        row = dict(clip=clip, present=os.path.exists(p), manifest_sha256=exp[clip])
        if not row['present']:
            rows.append(row); continue
        b = open(p, 'rb').read()
        row['sha256_raw'] = sha(b); row['match_raw'] = row['sha256_raw'] == exp[clip]
        row['sha256_norm'] = None; row['match_norm'] = row['match_raw']; row['isft_matched'] = None
        if not row['match_raw']:
            for v in good + [c for c in cands() if c not in good]:
                o = swap_isft(b, v)
                if o and sha(o) == exp[clip]:
                    row.update(sha256_norm=sha(o), match_norm=True, isft_matched='Lavf' + v)
                    if v not in good: good.append(v)
                    os.makedirs(a.norm_dir, exist_ok=True)
                    open(os.path.join(a.norm_dir, clip), 'wb').write(o)
                    break
            else:
                o = swap_isft(b, KNOWN[0])
                row['sha256_norm'] = sha(o) if o else None
        row.update(info(p))
        # manifest has only Apple LC-stream duration_s (not source frames)
        md = sorted(mdur[clip])
        row['manifest_stream_duration_s'] = md[0] if len(md) == 1 else md
        if len(md) == 1:
            d = round(md[0] * 48000) - row['frames']
            row['stream_minus_local_samples'] = d
            # LC stream = source + 2112 priming + pad to 1024 multiple -> d in [2112, 3136)
            row['length_consistent'] = 2112 <= d < 2112 + 1024
        rows.append(row)
    json.dump(dict(manifest=MAN, ffmpeg_used_to_build_corpus=m.get('ffmpeg_used_to_build_corpus'),
                   n=len(rows), present=sum(r['present'] for r in rows),
                   match_raw=sum(bool(r.get('match_raw')) for r in rows),
                   match_norm=sum(bool(r.get('match_norm')) for r in rows),
                   isft_versions_matched=good, rows=rows), open(os.path.join(a.out, 'hash_table.json'), 'w'), indent=1)
    L = ['# Corpus hash check', '',
         f"Manifest: `{MAN}` (corpus built with ffmpeg {m.get('ffmpeg_used_to_build_corpus')}); local ffmpeg 6.1.1.", '',
         f"present {sum(r['present'] for r in rows)}/{len(rows)}; raw match {sum(bool(r.get('match_raw')) for r in rows)}; "
         f"match after ISFT normalization {sum(bool(r.get('match_norm')) for r in rows)}; ISFT versions that matched: {good or 'none'}", '',
         'stream-local = (manifest Apple LC-stream duration_s - local duration) in samples @48k (not a source-length field; Apple adds 2112 priming + pad to 1024; consistent iff 2112<=d<3136).', f"length-consistent clips: {sum(bool(r.get('length_consistent')) for r in rows)}/{len(rows)}", '',
         '| clip | present | raw | norm | frames | dur s | rate/ch | RMS dBFS | stream-local smp | sha256 raw (16) | manifest (16) |',
         '|---|---|---|---|---|---|---|---|---|---|---|']
    for r in rows:
        if not r['present']:
            L.append(f"| {r['clip']} | no | | | | | | | | | {r['manifest_sha256'][:16]} |"); continue
        L.append(f"| {r['clip']} | y | {'Y' if r['match_raw'] else 'n'} | {'Y' if r['match_norm'] else 'n'} | {r['frames']} | {r['duration_s']:.4f} | "
                 f"{r['rate']}/{r['channels']} | {r['rms_dbfs']:.2f} | {r.get('stream_minus_local_samples','')} | {r['sha256_raw'][:16]} | {r['manifest_sha256'][:16]} |")
    open(os.path.join(a.out, 'hash_table.md'), 'w').write('\n'.join(L) + '\n')
    print(L[3])

if __name__ == '__main__':
    main()
