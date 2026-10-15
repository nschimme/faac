import json, math, statistics, sys
from pathlib import Path

bench = Path('/Users/nschimme/gitprojects/faac-benchmark')
sys.path[:0] = [str(bench), str(bench / 'scripts')]
from bd_rate import bd_rate_curve
from compare_results import bootstrap_mean_ci

root = Path('/private/tmp/faac-he-v1-adaptive/probe')
run_dir = root / 'results-all'
rows = json.loads((run_dir / 'noise-results.json').read_text())['rows']
config = json.loads((run_dir / 'noise-config.json').read_text())
clips = config['clips']
rates = config['rates_kbps']
comparisons = {}
for reference in ['static18', 'static24']:
    variant = 'adaptive18'
    per_clip = []
    packet_ties = pcm_ties = 0
    for clip in clips:
        base_points = [(rows[f'{reference}|{clip}|{rate}']['actual_payload_kbps'], rows[f'{reference}|{clip}|{rate}']['mos']) for rate in rates]
        candidate_points = [(rows[f'{variant}|{clip}|{rate}']['actual_payload_kbps'], rows[f'{variant}|{clip}|{rate}']['mos']) for rate in rates]
        per_clip.append({'clip': clip, 'bd_rate_percent': bd_rate_curve(base_points, candidate_points)})
        for rate in rates:
            ref_row, cand_row = rows[f'{reference}|{clip}|{rate}'], rows[f'{variant}|{clip}|{rate}']
            packet_ties += ref_row['payload_sha256'] == cand_row['payload_sha256']
            pcm_ties += ref_row['decoded_sha256'] == cand_row['decoded_sha256']
    bd_values = [x['bd_rate_percent'] for x in per_clip if x['bd_rate_percent'] is not None]
    bd_ci = bootstrap_mean_ci(bd_values)
    per_rate = {}
    for rate in rates:
        mos_deltas, bitrate_deltas = [], []
        for clip in clips:
            ref_row, cand_row = rows[f'{reference}|{clip}|{rate}'], rows[f'{variant}|{clip}|{rate}']
            mos_deltas.append(cand_row['mos'] - ref_row['mos'])
            bitrate_deltas.append(100 * (cand_row['actual_payload_kbps'] / ref_row['actual_payload_kbps'] - 1))
        ci = bootstrap_mean_ci(mos_deltas)
        per_rate[str(rate)] = {
            'mean_reference_mos': statistics.mean(rows[f'{reference}|{clip}|{rate}']['mos'] for clip in clips),
            'mean_candidate_mos': statistics.mean(rows[f'{variant}|{clip}|{rate}']['mos'] for clip in clips),
            'mean_mos_delta': statistics.mean(mos_deltas),
            'mos_delta_bootstrap_95_ci': ci,
            'mean_actual_bitrate_delta_percent': statistics.mean(bitrate_deltas),
        }
    comparisons[f'{variant}_vs_{reference}'] = {
        'valid_clips': len(bd_values), 'per_clip_bd_rate_percent': per_clip,
        'mean_bd_rate_percent': statistics.mean(bd_values),
        'median_bd_rate_percent': statistics.median(bd_values),
        'mean_bd_rate_bootstrap_95_ci': bd_ci,
        'per_rate': per_rate, 'rows': len(clips) * len(rates),
        'aac_packet_identity_ties': packet_ties,
        'decoded_pcm_identity_ties': pcm_ties,
        'ties_note': 'Packet and PCM equality are counted separately; output container metadata is excluded.',
    }

out = {'clips': len(clips), 'rates_kbps': rates, 'variants': ['static18', 'static24', 'adaptive18'],
       'comparison_method': 'Per-clip BD-rate from actual AAC packet bitrate and Zimtohrli MOS; bootstrap over clips. Per-rate MOS is paired by clip.',
       'comparisons': comparisons}
(root / 'direct-comparison.json').write_text(json.dumps(out, indent=2, sort_keys=True) + '\n')
lines = ['# Full-corpus adaptive noise comparison', '',
         f"Corpus: {len(clips)} clips at {', '.join(map(str, rates))} kbps. All rows use strict FDK decoding and Zimtohrli.",
         'BD-rate compares adaptive18 directly with each static control; negative favors adaptive18. Confidence intervals bootstrap over clips.', '']
lines += ['| Comparison | Mean BD-rate (95% CI) | Median | Packet ties | PCM ties |', '|---|---:|---:|---:|---:|']
for name, data in comparisons.items():
    ci = data['mean_bd_rate_bootstrap_95_ci']
    lines.append(f"| {name} | {data['mean_bd_rate_percent']:+.3f}% [{ci['lo']:+.3f}%, {ci['hi']:+.3f}%] | {data['median_bd_rate_percent']:+.3f}% | {data['aac_packet_identity_ties']}/{data['rows']} | {data['decoded_pcm_identity_ties']}/{data['rows']} |")
lines += ['', '## Per-rate paired MOS', '', '| Comparison | Rate | Reference MOS | Adaptive18 MOS | MOS delta (95% CI) | Actual bitrate delta |', '|---|---:|---:|---:|---:|---:|']
for name, data in comparisons.items():
    for rate, item in data['per_rate'].items():
        ci = item['mos_delta_bootstrap_95_ci']
        lines.append(f"| {name} | {rate} kbps | {item['mean_reference_mos']:.5f} | {item['mean_candidate_mos']:.5f} | {item['mean_mos_delta']:+.5f} [{ci['lo']:+.5f}, {ci['hi']:+.5f}] | {item['mean_actual_bitrate_delta_percent']:+.3f}% |")
lines += ['', '## Per-clip BD-rate', '', '| Comparison | Clip | BD-rate |', '|---|---|---:|']
for name, data in comparisons.items():
    for item in data['per_clip_bd_rate_percent']:
        lines.append(f"| {name} | {item['clip']} | {item['bd_rate_percent']:+.3f}% |")
lines += ['', 'Packet and decoded PCM identity counts exclude MP4/container metadata. This report describes benchmark results and does not establish a general optimum.', '']
(root / 'SUMMARY.md').write_text('\n'.join(lines))
print((root / 'SUMMARY.md').read_text())
