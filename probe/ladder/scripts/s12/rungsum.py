#!/usr/bin/env python3
"""Per-rung summary over harness rows: mean MOS, P1 (np.percentile 1), event-pooled NPER, stereo fidelity, kbps."""
import json, sys, argparse, numpy as np, collections
ap = argparse.ArgumentParser(); ap.add_argument('rows', nargs='+'); ap.add_argument('--exclude-clips')
ap.add_argument('--decoders', default='faad2,ffmpeg,fdkdec'); a = ap.parse_args()
ex = set(l.strip() for l in open(a.exclude_clips)) if a.exclude_clips else set()
D = collections.defaultdict(list)
for f in a.rows:
    for l in open(f):
        r = json.loads(l)
        if r.get('error') or r['clip'] in ex: continue
        D[(r['profile'], r['mode'] if r['arm'] not in ('apple', 'fdkref', 'xaacref') else 'ref', r['arm'], int(r['rung']), r['decoder'])].append(r)
print('| profile | mode | arm | rung | decoder | n | MOS | P1 | NPER pooled dB | events | stereo fid | kbps |')
print('|---|---|---|---|---|---|---|---|---|---|---|---|')
for k in sorted(D):
    if k[4] not in a.decoders.split(','): continue
    R = D[k]; m = [r['mos'] for r in R]
    ev = sum(r.get('nper_events') or 0 for r in R)
    np_ = sum((r.get('nper') or 0) * (r.get('nper_events') or 0) for r in R) / ev if ev else float('nan')
    sf = [r['stfid'] for r in R if r.get('stfid') is not None]
    print(f'| {k[0]} | {k[1]} | {k[2]} | {k[3]} | {k[4]} | {len(R)} | {np.mean(m):.4f} | {np.percentile(m,1):.3f} | {np_:.3f} | {ev} | {np.mean(sf) if sf else float("nan"):.4f} | {np.mean([r["kbps_realized"] for r in R]):.2f} |')
