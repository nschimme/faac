#!/Users/nschimme/gitprojects/faac-benchmark/.venv/bin/python
"""Serial HE 48k injected arms with resumable per-arm results."""
import argparse
import fcntl
import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import time

import numpy as np
import soundfile as sf

ROOT = pathlib.Path(__file__).resolve().parents[4]
LADDER = ROOT / 'probe/ladder'
AUDIO = pathlib.Path('/Users/nschimme/gitprojects/faac-benchmark/data/external/audio')
FAAC = ROOT / 'build_ladder/frontend/faac'
FAAD = pathlib.Path('/private/tmp/claude-501/faac-work/faad-dump/build-ladder/frontend/faad')
SCORE = pathlib.Path('/Users/nschimme/gitprojects/faac-benchmark/scripts/score_clip.py')
CONVERT = LADDER / 'scripts/h/h_conv.py'
APPLE = LADDER / 'ref/apple_he48k'
DEFAULT_OUT = LADDER / 'results/s9/arms'
BASE = {'FAAC_SF_SMOOTH': '0.6', 'FAAC_BS_DROPRATIO': '12',
        'FAAC_SBR_FREQ_SCALE': '3', 'NUMBA_DISABLE_JIT': '1'}
ALIGN = {'fdk': (2015, 1), 'apple': (96, 2)}
ARMS = {
    'F_fdk': ('fdk', None, 48), 'F_apple': ('apple', None, 48),
    'W_fdk': ('fdk', 'win', 48), 'W_apple': ('apple', 'win', 48),
    'MS_fdk': ('fdk', 'ms', 48), 'MS_apple': ('apple', 'ms', 48),
    'WMS_fdk': ('fdk', 'win,ms', 48), 'WMS_apple': ('apple', 'win,ms', 48),
    'WS_fdk': ('fdk', 'win,sf', 48), 'WS_apple': ('apple', 'win,sf', 48),
    'F40': (None, None, 40), 'F56': (None, None, 56),
    'REF_fdk': ('fdk', None, None), 'REF_apple': ('apple', None, None),
    'F0': (None, None, 48),
}

def run(argv, env=None):
    return subprocess.run([str(x) for x in argv], env=env, check=True,
                          capture_output=True, text=True)

def encoder_env(reference=None, fields=None, ci=None):
    env = os.environ.copy()
    for key in list(env):
        if key.startswith('FAAC_CORE_INJECT'):
            env.pop(key)
    env.update(BASE)
    if fields:
        env.update(FAAC_CORE_INJECT=str(ci), FAAC_CORE_INJECT_FIELDS=fields,
                   FAAC_CORE_INJECT_OFFSET=str(ALIGN[reference][1]),
                   FAAC_CORE_INJECT_LOOSE_SFB='1')
    return env

def profile(path):
    data = json.loads(run(['ffprobe', '-v', 'error', '-select_streams', 'a:0',
                           '-show_entries', 'stream=profile,sample_rate',
                           '-of', 'json', path]).stdout)
    streams = data.get('streams', [])
    if len(streams) != 1 or 'HE-AAC' not in streams[0].get('profile', '') or \
            int(streams[0].get('sample_rate', 0)) != 48000:
        raise ValueError(f'Expected 48 kHz HE-AAC: {path}: {streams}')
    return streams[0]['profile']

def dump(aac, dump_path, scratch):
    adts = scratch / 'input.aac'
    wav = scratch / 'decoded.wav'
    dump_path.unlink(missing_ok=True)
    run(['ffmpeg', '-v', 'error', '-y', '-i', aac, '-c:a', 'copy', '-f', 'adts', adts])
    env = os.environ.copy()
    env.update(FAAD_DUMP=str(dump_path), FAAD_LADDER_DUMP='1')
    run([FAAD, '-o', wav, adts], env)

def shares(dump_path):
    frames = {}
    for line in dump_path.open():
        if not line.startswith('C '):
            continue
        head, bands = line.split('|', 1)
        tokens = head.split()
        frame, ch, window = int(tokens[1]), int(tokens[2]), int(tokens[4])
        values = [(int(cb), int(ms)) for cb, sf, nnz, ms in
                  re.findall(r'(\d+):(-?\d+):(\d+):(\d+)', bands)]
        frames.setdefault(frame, {})[ch] = (window, values)
    short = total = ms_count = regular = 0
    for channels in frames.values():
        for window, _ in channels.values():
            total += 1
            short += window == 2
        if 0 in channels and channels[0][0] != 2:
            # Stage S6 reports M/S on regular coded bands of the left long ICS.
            for cb, ms in channels[0][1]:
                if 1 <= cb <= 11:
                    regular += 1
                    ms_count += ms != 0
    if not total:
        raise ValueError(f'No C records in {dump_path}')
    return {'short_ics': short, 'total_ics': total,
            'ms_regular_bands': ms_count, 'regular_stereo_bands': regular,
            'short_share': short / total,
            'ms_share': ms_count / regular if regular else None}

def padded(src, dst, samples):
    audio, rate = sf.read(src)
    if rate != 48000:
        raise ValueError(f'Expected 48 kHz input: {src}')
    zeros = np.zeros((samples,) + audio.shape[1:], dtype=audio.dtype)
    sf.write(dst, np.concatenate((zeros, audio)), rate)

def reference(name, src, cache, fdk_binary):
    stem = src.stem
    directory = cache / name
    directory.mkdir(parents=True, exist_ok=True)
    lock_path = directory / f'{stem}.lock'
    with lock_path.open('a+') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        if name == 'fdk':
            encoded = directory / f'{stem}.m4a'
            if not encoded.exists():
                with tempfile.TemporaryDirectory(dir=directory) as temp:
                    tmp = pathlib.Path(temp) / 'reference.m4a'
                    run([fdk_binary, '-S', '-p', '5', '-b', '48000', '-o', tmp, src])
                    profile(tmp)
                    os.replace(tmp, encoded)
        else:
            encoded = APPLE / f'{stem}.m4a'
            if not encoded.exists():
                raise FileNotFoundError(encoded)
        profile(encoded)
        record = directory / f'{stem}.dump'
        ci = directory / f'{stem}.ci'
        if not record.exists() or not ci.exists():
            with tempfile.TemporaryDirectory(dir=directory) as temp:
                scratch = pathlib.Path(temp)
                tmp_dump = scratch / 'reference.dump'
                tmp_ci = scratch / 'reference.ci'
                dump(encoded, tmp_dump, scratch)
                run([sys.executable, CONVERT, tmp_dump, tmp_ci])
                os.replace(tmp_dump, record)
                os.replace(tmp_ci, ci)
        return encoded, record, ci

def score(src, encoded):
    env = os.environ.copy()
    env['NUMBA_DISABLE_JIT'] = '1'
    output = run([sys.executable, SCORE, src, encoded], env).stdout
    match = re.search(r'MOS:\s*([0-9.]+)', output)
    if not match:
        raise ValueError(f'No MOS in score_clip output: {output}')
    return float(match.group(1))

def atomic_json(path, value):
    temp = path.with_name(f'{path.name}.{os.getpid()}.tmp')
    temp.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')
    os.replace(temp, path)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--arm', required=True, choices=ARMS)
    parser.add_argument('--limit', type=int, default=49)
    parser.add_argument('--out', type=pathlib.Path, default=DEFAULT_OUT)
    parser.add_argument('--fdk', default='fdkaac')
    parser.add_argument('--refresh-stats', action='store_true')
    args = parser.parse_args()
    if args.limit < 1:
        parser.error('--limit must be positive')
    if not FAAC.exists() or not FAAD.exists():
        raise FileNotFoundError('Build the probe and FAAD ladder decoder first')
    os.environ['NUMBA_DISABLE_JIT'] = '1'
    args.out.mkdir(parents=True, exist_ok=True)
    cache = args.out / 'cache'
    cache.mkdir(exist_ok=True)
    results_path = args.out / f'{args.arm}.json'
    rows = json.loads(results_path.read_text()) if results_path.exists() else {}
    ref_name, fields, bitrate = ARMS[args.arm]
    clips = sorted(AUDIO.glob('*.wav'))[:args.limit]
    for src in clips:
        if args.refresh_stats:
            if src.name not in rows:
                continue
            if args.arm.startswith('REF_'):
                _, record, _ = reference(ref_name, src, cache, args.fdk)
                stats = shares(record)
            else:
                encoded = args.out / 'enc' / args.arm / f'{src.stem}.m4a'
                with tempfile.TemporaryDirectory(dir=cache) as temp:
                    scratch = pathlib.Path(temp)
                    record = scratch / 'arm.dump'
                    dump(encoded, record, scratch)
                    stats = shares(record)
            rows[src.name].update(stats)
            atomic_json(results_path, rows)
            print(args.arm, src.name, 'stats refreshed', flush=True)
            continue
        if src.name in rows:
            continue
        started = time.monotonic()
        encoded = None
        ci = None
        if ref_name and (fields or args.arm.startswith('REF_')):
            encoded_ref, ref_dump, ci = reference(ref_name, src, cache, args.fdk)
        if args.arm.startswith('REF_'):
            encoded = encoded_ref
            stats = shares(ref_dump)
        else:
            arm_dir = args.out / 'enc' / args.arm
            arm_dir.mkdir(parents=True, exist_ok=True)
            encoded = arm_dir / f'{src.stem}.m4a'
            with tempfile.TemporaryDirectory(dir=arm_dir) as temp:
                scratch = pathlib.Path(temp)
                pad = ALIGN[ref_name][0] if ref_name else 0
                source = src
                if pad:
                    source = scratch / 'padded.wav'
                    padded(src, source, pad)
                run([FAAC, '--overwrite', '--object-type', 'he-aac-v1',
                     '-b', str(bitrate), '-o', encoded, source],
                    encoder_env(ref_name, fields, ci))
                profile(encoded)
                arm_dump = scratch / 'arm.dump'
                dump(encoded, arm_dump, scratch)
                stats = shares(arm_dump)
        mos = score(src, encoded)
        rows[src.name] = {'mos': mos, 'bytes': encoded.stat().st_size,
                          'seconds': round(time.monotonic() - started, 3), **stats}
        atomic_json(results_path, rows)
        print(args.arm, src.name, json.dumps(rows[src.name], sort_keys=True), flush=True)
    print(f'{args.arm}: {len(rows)} saved clips in {results_path}', flush=True)

if __name__ == '__main__':
    main()
