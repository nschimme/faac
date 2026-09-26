#!/usr/bin/env python3
"""Serial gate-5 scorer for FAAC using FDK's per-channel SBR grids."""
import argparse
import csv
import os
import re
import subprocess
from pathlib import Path

FULL = Path('/private/tmp/claude-501/faac-work/faad3/probe/transplant-full')
FAAC = Path('build/frontend/faac')
FAAD = Path('/private/tmp/claude-501/faac-work/faad3/bs/frontend/faad')
SCORER = Path('/Users/nschimme/gitprojects/faac-benchmark/scripts/align/sc.py')
PYTHON = Path('/Users/nschimme/gitprojects/faac-benchmark/.venv/bin/python')
OUT = Path('probe_out/grid.csv')
FIELDS = ('clip', 'rate', 'arm', 'score', 'bytes', 'grid_match_ch0', 'grid_match_ch1')

def run(cmd, env=None):
    p = subprocess.run([str(x) for x in cmd], text=True, stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE, env={**os.environ, **(env or {})})
    if p.returncode:
        raise RuntimeError(f'command failed ({p.returncode}): {" ".join(map(str, cmd))}\n{p.stderr[-4000:]}')
    return p.stdout

def stem(clip):
    return re.sub(r'[^A-Za-z0-9._-]+', '_', clip.stem).strip('_')

def dump(path):
    """Return class/count, grid, and freq-res records keyed by (frame, channel)."""
    result = {}
    for line in path.read_text().splitlines():
        words = line.split()
        if not words or words[0] not in ('F', 'G', 'R'):
            continue
        frame, channel = map(int, words[1:3]); record = result.setdefault((frame, channel), {})
        if words[0] == 'F':
            record['f'] = tuple(map(int, words[3:5]))
        elif words[0] == 'G':
            record['g'] = tuple(map(int, words[3:]))
        else:
            m = re.search(r'freq_res: ([^|]+)', line)
            record['r'] = tuple(map(int, m.group(1).split())) if m else ()
    return result

SKIPPED = [0, 0]

def grid_match(donor, encoded):
    donor, encoded = dump(donor), dump(encoded)
    answer = []
    for channel in (0, 1):
        total = matched = 0
        for (frame, ch), got in encoded.items():
            # Frames 2-3 precede the first injected payload (SBR FIFO priming).
            if ch != channel or frame < 4 or not {'f', 'g', 'r'} <= got.keys():
                continue
            expected = donor.get((frame + 1, channel), {}) # calibrated FDK +1
            # FAAC's final flush payload has no corresponding padded-FDK
            # access unit. It is not an aligned frame and must not dilute the
            # injection-grid gate.
            if not {'f', 'g', 'r'} <= expected.keys():
                continue
            # The probe writer caps at 4 envelopes; fdk's 5-envelope VARVAR
            # frames are counted separately, not gated.
            if expected['f'][1] > 4:
                SKIPPED[channel] += 1
                continue
            total += 1
            if all(got[k] == expected.get(k) for k in ('f', 'g', 'r')):
                matched += 1
        if not total:
            raise RuntimeError(f'no channel-{channel} SBR grids in {encoded}')
        answer.append(100.0 * matched / total)
    return answer

def score(ref, deg):
    text = run([PYTHON, SCORER, ref, deg], {'NUMBA_DISABLE_JIT': '1'})
    values = re.findall(r'[-+]?\d+(?:\.\d+)?', text)
    if not values:
        raise RuntimeError(f'scorer emitted no value: {text!r}')
    return float(values[-1])

def completed():
    if not OUT.exists():
        return set()
    with OUT.open() as f:
        return {(r['clip'], int(r['rate']), r['arm']) for r in csv.DictReader(f)}

def append(row):
    first = not OUT.exists()
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open('a', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=FIELDS)
        if first: writer.writeheader()
        writer.writerow(row)

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--rates', default='64,96')
    p.add_argument('--limit', type=int, help='number of unscored clips to run')
    args = p.parse_args()
    rates = {int(x) for x in args.rates.split(',')}
    source = list(csv.DictReader((FULL / 'results.csv').open()))
    clips = sorted({Path(r['clip']) for r in source if int(r['rate']) in rates})
    done = completed(); ran = 0
    for clip in clips:
        for rate in sorted(rates):
            key = (str(clip), rate, 'faac_fdkgrid')
            if key in done:
                continue
            matches = list((FULL / 'clips').glob(f'[0-9][0-9]_{stem(clip)}/{rate}/fdk.aac'))
            if len(matches) != 1:
                raise RuntimeError(f'cannot identify FDK stream for {clip}, {rate}: {matches}')
            d = Path('probe_out/grid_work') / f'{stem(clip)}_{rate}'
            d.mkdir(parents=True, exist_ok=True)
            donor_dump, encoded, encoded_dump, wav = (d / 'fdk.dump', d / 'faac_fdkgrid.aac',
                                                        d / 'faac_fdkgrid.dump', d / 'faac_fdkgrid.wav')
            if not donor_dump.exists():
                run([FAAD, '-q', '-b', '32f', '-o', d / 'fdk.wav', matches[0]], {'FAAD_DUMP': str(donor_dump)})
            encoded_dump.unlink(missing_ok=True)
            run([FAAC, '--overwrite', '--object-type', 'he-aac-v1', '-b', rate, '-o', encoded, clip],
                {'FAAC_SBR_INJECT': str(donor_dump), 'FAAC_SBR_INJECT_FIELDS': 'grid', 'FAAC_SBR_INJECT_OFFSET': '1'})
            run([FAAD, '-q', '-b', '32f', '-o', wav, encoded], {'FAAD_DUMP': str(encoded_dump)})
            SKIPPED[:] = [0, 0]
            m0, m1 = grid_match(donor_dump, encoded_dump)
            print(f'{clip.name} {rate}: 5-env frames skipped ch0={SKIPPED[0]} ch1={SKIPPED[1]}', flush=True)
            if min(m0, m1) < 98.0:
                print(f'HARNESS ERROR {clip} {rate}: grid ch0={m0:.3f}% ch1={m1:.3f}%', flush=True)
                return 2
            value = score(clip, wav)
            row = {'clip': str(clip), 'rate': rate, 'arm': 'faac_fdkgrid', 'score': value,
                   'bytes': encoded.stat().st_size, 'grid_match_ch0': f'{m0:.6f}', 'grid_match_ch1': f'{m1:.6f}'}
            append(row); print(','.join(str(row[x]) for x in FIELDS), flush=True)
            ran += 1
            if args.limit is not None and ran >= args.limit:
                return 0
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
