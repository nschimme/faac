import json, statistics, sys
from pathlib import Path

bench = Path('/Users/nschimme/gitprojects/faac-benchmark')
sys.path[:0] = [str(bench), str(bench / 'scripts')]
from bd_rate import bd_rate_curve
from compare_results import bootstrap_mean_ci

rows = json.loads(Path('/private/tmp/faac-he-v1-adaptive/probe/results-gate/noise-results.json').read_text())['rows']
clips = ['sandman.16b48k.wav', 'velvet.16b48k.wav', '21-classic.441.16b48k.wav', 'fms.wav']
rates = [32, 48, 64, 96]
variants = ['static18', 'static24', 'adaptive_wide', 'adaptive12', 'adaptive18', 'adaptive18_low_offset', 'adaptive18_high_offset']
for base in ['baseline_original', 'static18', 'static24']:
    print('AGAINST', base)
    for variant in variants:
        if variant == base:
            continue
        values = []
        for clip in clips:
            points_base = [(rows[f'{base}|{clip}|{rate}']['actual_payload_kbps'], rows[f'{base}|{clip}|{rate}']['mos']) for rate in rates]
            points_variant = [(rows[f'{variant}|{clip}|{rate}']['actual_payload_kbps'], rows[f'{variant}|{clip}|{rate}']['mos']) for rate in rates]
            values.append(bd_rate_curve(points_base, points_variant))
        ci = bootstrap_mean_ci(values)
        print(f'  {variant}: mean={statistics.mean(values):+.4f}%, median={statistics.median(values):+.4f}%, CI=[{ci["lo"]:+.4f}%, {ci["hi"]:+.4f}%], clips={[round(x,4) for x in values]}')
        for rate in rates:
            deltas = [rows[f'{variant}|{clip}|{rate}']['mos'] - rows[f'{base}|{clip}|{rate}']['mos'] for clip in clips]
            rate_deltas = [100 * (rows[f'{variant}|{clip}|{rate}']['actual_payload_kbps'] / rows[f'{base}|{clip}|{rate}']['actual_payload_kbps'] - 1) for clip in clips]
            mos_ci = bootstrap_mean_ci(deltas)
            print(f'    {rate}: MOS delta={statistics.mean(deltas):+.5f} [{mos_ci["lo"]:+.5f},{mos_ci["hi"]:+.5f}], actual bitrate delta={statistics.mean(rate_deltas):+.3f}%')
