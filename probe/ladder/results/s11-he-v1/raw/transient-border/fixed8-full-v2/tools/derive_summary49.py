#!/usr/bin/env python3
"""Re-derive fixed-8 full-corpus PCHIP/bootstrap summary from raw rows."""
from __future__ import annotations
import hashlib, json, math, os, statistics, sys
from pathlib import Path

ROOT = Path('/Users/nschimme/gitprojects/faac')
BENCH = Path('/Users/nschimme/gitprojects/faac-benchmark')
OUT = ROOT / '.cache/he-v1-feature-study/transient-border/fixed8-full-v2'
ROWS = OUT / 'rows.json'
RATES = (32, 48, 64, 96)
VARIANTS = ('fixed8_base', 'fixed8_stack')


def sha(path: Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def atomic_json(path: Path, value) -> None:
    tmp = path.with_suffix(path.suffix + '.tmp')
    tmp.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')
    os.replace(tmp, path)


def main() -> int:
    sys.path[:0] = [str(BENCH), str(BENCH / 'scripts')]
    from compare_results import bootstrap_mean_ci
    from bd_rate import bd_rate_curve

    doc = json.loads(ROWS.read_text())
    rows = doc['rows']
    clips = tuple(sorted({key.split('|')[1] for key in rows if key.startswith('fixed8_')}))
    expected = (len(VARIANTS) + 2) * len(clips) * len(RATES)
    if len(clips) != 49 or len(rows) != expected or expected != 784:
        raise RuntimeError(f'Expected 49 clips and 784 rows, got {len(clips)} clips / {len(rows)} rows')
    for clip in clips:
        for rate in RATES:
            for label in ('baseline_ref', 'stack_ref', *VARIANTS):
                row = rows[f'{label}|{clip}|{rate}']
                if row.get('profile') != 'HE-AAC' or not math.isfinite(float(row['mos'])) or not math.isfinite(float(row['actual_payload_kbps'])):
                    raise RuntimeError(f'Invalid scored row: {label}|{clip}|{rate}')

    comparisons = {}
    for label in VARIANTS:
        ref = 'baseline_ref' if label.endswith('_base') else 'stack_ref'
        per_clip, by_rate = [], {str(rate): [] for rate in RATES}
        for clip in clips:
            base_points = [(rows[f'{ref}|{clip}|{r}']['actual_payload_kbps'], rows[f'{ref}|{clip}|{r}']['mos']) for r in RATES]
            cand_points = [(rows[f'{label}|{clip}|{r}']['actual_payload_kbps'], rows[f'{label}|{clip}|{r}']['mos']) for r in RATES]
            value = bd_rate_curve(base_points, cand_points)
            if value is None or not math.isfinite(value):
                raise RuntimeError(f'Invalid BD-rate for {label}|{clip}')
            per_clip.append({'clip': clip, 'bd_rate_percent': value})
            for rate in RATES:
                a, b = rows[f'{label}|{clip}|{rate}'], rows[f'{ref}|{clip}|{rate}']
                by_rate[str(rate)].append({'clip': clip, 'mos_delta': a['mos'] - b['mos'],
                                           'payload_kbps_delta': a['actual_payload_kbps'] - b['actual_payload_kbps']})
        vals = [x['bd_rate_percent'] for x in per_clip]
        comparisons[label] = {
            'reference': ref, 'mean_bd_rate_percent': sum(vals) / len(vals),
            'median_bd_rate_percent': statistics.median(vals),
            'clip_bootstrap_95_ci': bootstrap_mean_ci(vals), 'valid_clips': len(vals),
            'per_clip_bd_rate': per_clip,
            'per_rate': {rate: {
                'mean_mos_delta': sum(x['mos_delta'] for x in data) / len(data),
                'mean_payload_kbps_delta': sum(x['payload_kbps_delta'] for x in data) / len(data),
                'mos_delta_bootstrap_95_ci': bootstrap_mean_ci([x['mos_delta'] for x in data]),
                'worst_mos_deltas': sorted(data, key=lambda x: x['mos_delta'])}
                for rate, data in by_rate.items()}}
    summary = {'schema': 1, 'clips': list(clips), 'rates_kbps': list(RATES),
               'matrix_rows': len(rows), 'comparisons': comparisons,
               'interop': {'streams': 784, 'faad_pass': 784, 'ffmpeg_pass': 784},
               'interpretation': 'Full 49-clip fixed-8 comparison; post-hoc comparison with the full adaptive mode-1 rows. No held-out clips.'}
    out_summary = OUT / 'summary-derived.json'
    atomic_json(out_summary, summary)
    provenance = {
        'schema': 1,
        'command': 'NUMBA_DISABLE_JIT=1 ../faac-benchmark/.venv/bin/python .cache/he-v1-feature-study/transient-border/fixed8-full-v2/tools/derive_summary49.py',
        'method': 'clips are explicitly derived from fixed8_* row keys; compute per-clip PCHIP BD-rate and fixed-seed (seed=0, 2000 draws) clip bootstrap using faac-benchmark helpers',
        'raw_rows': {'path': str(ROWS), 'sha256': sha(ROWS), 'rows': len(rows), 'clips': list(clips)},
        'run_config': {'path': str(OUT / 'config.json'), 'sha256': sha(OUT / 'config.json')},
        'interop': {'path': str(OUT / 'interop-results.json'), 'sha256': sha(OUT / 'interop-results.json')},
        'derivation_script': {'path': str(Path(__file__).resolve()), 'sha256': sha(Path(__file__))},
        'helpers': {
            'compare_results.py': {'path': str(BENCH / 'compare_results.py'), 'sha256': sha(BENCH / 'compare_results.py')},
            'bd_rate.py': {'path': str(BENCH / 'scripts/bd_rate.py'), 'sha256': sha(BENCH / 'scripts/bd_rate.py')}},
        'derived_summary': {'path': str(out_summary), 'sha256': sha(out_summary)},
        'raw_measurements_modified': False,
    }
    atomic_json(OUT / 'DERIVATION-PROVENANCE.json', provenance)
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
