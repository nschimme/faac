#!/Users/nschimme/gitprojects/faac-benchmark/.venv/bin/python
"""Per-clip 40/56 slope adjustment for HE 48k injection arms."""
import argparse
import json
import math
import pathlib
import statistics

ROOT = pathlib.Path(__file__).resolve().parents[4]
DEFAULT = ROOT / 'probe/ladder/results/s9/arms'

def load(directory, arm):
    path = directory / f'{arm}.json'
    return json.loads(path.read_text()) if path.exists() else {}

def shares(rows, clips):
    short = sum(rows[c]['short_ics'] for c in clips)
    total = sum(rows[c]['total_ics'] for c in clips)
    mid = sum(rows[c]['ms_regular_bands'] for c in clips)
    regular = sum(rows[c]['regular_stereo_bands'] for c in clips)
    return (short / total if total else float('nan'),
            mid / regular if regular else float('nan'))

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', type=pathlib.Path, default=DEFAULT)
    args = parser.parse_args()
    low, high = load(args.out, 'F40'), load(args.out, 'F56')
    for reference in ('fdk', 'apple'):
        base = load(args.out, f'F_{reference}')
        ref = load(args.out, f'REF_{reference}')
        raw_clips = sorted(base.keys() & ref.keys())
        if raw_clips:
            raw = [ref[c]['mos'] - base[c]['mos'] for c in raw_clips]
            bytes_pct = 100 * (sum(ref[c]['bytes'] for c in raw_clips) /
                               sum(base[c]['bytes'] for c in raw_clips) - 1)
            print(f'{reference} raw MOS - F: n={len(raw)} mean={statistics.mean(raw):+.4f} '
                  f'median={statistics.median(raw):+.4f} bytes={bytes_pct:+.2f}%')
        else:
            print(f'{reference} raw MOS - F: no shared clips')
        for prefix in ('W', 'MS', 'WMS', 'WS'):
            arm_name = f'{prefix}_{reference}'
            arm = load(args.out, arm_name)
            clips = sorted(base.keys() & arm.keys() & low.keys() & high.keys())
            if not clips:
                print(f'{arm_name}: no complete clips')
                continue
            adjusted = []
            for clip in clips:
                lo, hi, b, x = low[clip], high[clip], base[clip], arm[clip]
                width = math.log2(hi['bytes'] / lo['bytes'])
                if width == 0:
                    raise ValueError(f'Zero 40/56 slope width: {clip}')
                slope = (hi['mos'] - lo['mos']) / width
                gain = x['mos'] - b['mos'] - slope * math.log2(x['bytes'] / b['bytes'])
                adjusted.append((gain, clip))
            values = [value for value, _ in adjusted]
            bytes_pct = 100 * (sum(arm[c]['bytes'] for c in clips) /
                               sum(base[c]['bytes'] for c in clips) - 1)
            short, mid = shares(arm, clips)
            label = ' [rate-loop SF diagnostic]' if prefix == 'WS' else ''
            print(f'{arm_name}{label}: n={len(clips)} adjusted_mean={statistics.mean(values):+.4f} '
                  f'median={statistics.median(values):+.4f} '
                  f'W/L={sum(x > 0 for x in values)}/{sum(x < 0 for x in values)} '
                  f'bytes={bytes_pct:+.2f}% short={short:.2%} MS_long_regular={mid:.2%}')
            for gain, clip in sorted(adjusted)[:3]:
                print(f'  worst {gain:+.4f} {clip}')
    control, fdk_base = load(args.out, 'F0'), load(args.out, 'F_fdk')
    common = sorted(control.keys() & fdk_base.keys())
    if common:
        difference = max(abs(control[c]['mos'] - fdk_base[c]['mos']) for c in common)
        print(f'pad 0 vs 2015 MOS: n={len(common)} max_abs_diff={difference:.4f} '
              f'gate={"PASS" if difference <= .01 else "FAIL"}')

if __name__ == '__main__':
    main()
