import json, os, re, statistics, sys, tempfile
from pathlib import Path

os.environ.setdefault('NUMBA_DISABLE_JIT', '1')
bench = Path('/Users/nschimme/gitprojects/faac-benchmark')
probe = Path('/private/tmp/faac-he-v1-adaptive')
sys.path[:0] = [str(bench), str(probe / 'probe')]
from codec_bench.encoders import FAACEncoder
from phase2_mos import score_wav_pair  # noqa: F401
import run_noise

clips = ['sandman.16b48k.wav', 'velvet.16b48k.wav', '21-classic.441.16b48k.wav', 'fms.wav']
rate = 64
variant_env = {'FAAC_SBR_ADAPTIVE_NOISE':'1', 'FAAC_SBR_NOISE_MIN_LEVEL':'18',
               'FAAC_SBR_NOISE_MAX_LEVEL':'24', 'FAAC_SBR_NOISE_OFFSET_LOG2':'-8'}
source_dir = bench / 'data/external/audio'
trace_dir = probe / 'probe' / 'noise-trace'
trace_dir.mkdir(parents=True, exist_ok=True)
gate_rows = json.loads((probe / 'probe/results-gate/noise-results.json').read_text())['rows']
pattern = re.compile(r'SBR noise ch=(\d+) env=(\d+) Torig=([\deE+.-]+) Tsource=([\deE+.-]+) rawQ=([\deE+.-]+) smoothedQ=([\deE+.-]+) finalLevel=(\d+)')
report = {'rate_kbps': rate, 'variant': 'adaptive18', 'trace_environment': {'FAAC_SBR_NOISE_TRACE':'1'}, 'clips': {}}

for clip in clips:
    src = source_dir / clip
    out = trace_dir / (Path(clip).stem + '.m4a')
    encoder = FAACEncoder('faac-noise-trace', str(probe / 'build-probe/frontend/faac'), profile='he')
    cmd = encoder.get_encode_cmd(str(src), str(out), rate, 2, 48000)
    if cmd[cmd.index('--object-type') + 1] != 'he-aac-v1':
        raise RuntimeError('Expected HE-AAC v1')
    env = dict(os.environ)
    for key in list(env):
        if key.startswith('FAAC_SBR_'):
            env.pop(key)
    env.update(variant_env)
    env['FAAC_SBR_NOISE_TRACE'] = '1'
    result = run_noise.run(cmd, env=env)
    if run_noise.profile(out) != 'HE-AAC':
        raise RuntimeError(f'Unexpected profile for {clip}')
    packet_hash = run_noise.payload_identity(out)
    expected = gate_rows[f'adaptive18|{clip}|{rate}']
    if packet_hash != expected['payload_sha256']:
        raise RuntimeError(f'Trace changed AAC packets for {clip}: {packet_hash} != {expected["payload_sha256"]}')
    matches = [m for m in pattern.finditer(result.stderr) if all(map(lambda x: __import__('math').isfinite(float(x)), m.groups()[2:6]))]
    if not matches:
        raise RuntimeError(f'No finite trace rows for {clip}')
    levels = {}
    for match in matches:
        ch, env_idx, torig, tsource, rawq, smoothq, level = match.groups()
        levels.setdefault(ch, {}).setdefault(level, 0)
        levels[ch][level] += 1
    nums = [{'ch': int(m.group(1)), 'env': int(m.group(2)), 'Torig': float(m.group(3)),
             'Tsource': float(m.group(4)), 'rawQ': float(m.group(5)),
             'smoothedQ': float(m.group(6)), 'level': int(m.group(7))} for m in matches]
    report['clips'][clip] = {
        'packet_identity_matches_gate_adaptive18': True,
        'packet_sha256': packet_hash,
        'trace_rows': len(nums),
        'level_histogram_by_channel': levels,
        'level_min': min(x['level'] for x in nums), 'level_max': max(x['level'] for x in nums),
        'Torig_min': min(x['Torig'] for x in nums), 'Torig_max': max(x['Torig'] for x in nums),
        'Tsource_min': min(x['Tsource'] for x in nums), 'Tsource_max': max(x['Tsource'] for x in nums),
        'stderr_trace_rows': nums,
    }

(trace_dir / 'trace-summary.json').write_text(json.dumps(report, indent=2, sort_keys=True) + '\n')
print(json.dumps({c:{k:v for k,v in x.items() if k!='stderr_trace_rows'} for c,x in report['clips'].items()}, indent=2))
