#!/usr/bin/env python3
"""Serially re-score the established decoder-side control WAVs."""
import csv
import os
import re
import subprocess
from collections import defaultdict
from pathlib import Path

ROOT = Path('/private/tmp/claude-501/faac-work/faad3/probe/transplant-full')
SCORER = Path('/Users/nschimme/gitprojects/faac-benchmark/scripts/align/sc.py')
PYTHON = Path('/Users/nschimme/gitprojects/faac-benchmark/.venv/bin/python')
OUT = Path('probe_out/controls.csv')
ARMS = ('faac', 'fdk', 'all', 'hdr_grid_env')

def score(ref, deg):
    p = subprocess.run([str(PYTHON), str(SCORER), str(ref), str(deg)], text=True,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                       env={**os.environ, 'NUMBA_DISABLE_JIT': '1'})
    if p.returncode:
        raise RuntimeError(p.stderr)
    return float(re.findall(r'[-+]?\d+(?:\.\d+)?', p.stdout)[-1])

source_rows = list(csv.DictReader((ROOT / 'results.csv').open()))
seen = set(); rows = []
for row in source_rows:
    arm, rate, clip = row['arm'], int(row['rate']), Path(row['clip'])
    key = (clip, rate, arm)
    if arm not in ARMS or key in seen:
        continue
    seen.add(key)
    stem = re.sub(r'[^A-Za-z0-9._-]+', '_', clip.stem).strip('_')
    matches = list((ROOT / 'clips').glob(f'[0-9][0-9]_{stem}/{rate}/{arm}.wav'))
    if len(matches) != 1:
        raise RuntimeError(f'cannot identify control WAV for {key}: {matches}')
    value = score(clip, matches[0])
    rows.append({'clip': str(clip), 'rate': rate, 'arm': arm, 'score': value,
                 'wav': str(matches[0])})
    print(f'{rate} {arm} {clip.name}: {value:.6f}', flush=True)

OUT.parent.mkdir(exist_ok=True)
with OUT.open('w', newline='') as f:
    w = csv.DictWriter(f, fieldnames=list(rows[0])); w.writeheader(); w.writerows(rows)

by = defaultdict(dict)
for row in rows: by[(row['rate'], row['clip'])][row['arm']] = row['score']
summary = []
for rate in (64, 96):
    for arm in ARMS:
        values = [v[arm] for (r, _), v in by.items() if r == rate]
        base = [v['faac'] for (r, _), v in by.items() if r == rate]
        diffs = [x-y for x, y in zip(values, base)]
        summary.append({'rate': rate, 'arm': arm, 'mean': sum(values)/len(values),
                        'delta_vs_faac': sum(diffs)/len(diffs),
                        'wins': sum(x > .02 for x in diffs), 'losses': sum(x < -.02 for x in diffs),
                        'n': len(values)})
with Path('probe_out/controls_summary.csv').open('w', newline='') as f:
    w = csv.DictWriter(f, fieldnames=list(summary[0])); w.writeheader(); w.writerows(summary)
