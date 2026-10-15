#!/usr/bin/env python3
"""Encode and strictly validate the frozen 098 mode-1 probe gate."""
import hashlib
import json
import os
import subprocess
import sys
import wave
from pathlib import Path

REPO = Path('/private/tmp/faac-upstream098-adaptive-border')
BASE = Path('/private/tmp/faac-upstream098-baseline/build-release/frontend/faac')
PROBE = REPO / 'build-release/frontend/faac'
BENCH = Path('/Users/nschimme/gitprojects/faac-benchmark')
FAAD = Path('/private/tmp/faac-he-v1-decoder-build/frontend/faad')
FFMPEG = Path('/opt/homebrew/bin/ffmpeg')
OUT = REPO / 'probe-results'
CLIPS = ('sandman.16b48k.wav', 'velvet.16b48k.wav',
         '21-classic.441.16b48k.wav', 'fms.wav')
RATES = (32, 48, 64, 96)

sys.path.insert(0, str(BENCH))
from codec_bench.encoders import FAACEncoder
from codec_bench.decoders import FDKDecoder
import utils

FDK = FDKDecoder('FDK AAC strict', str(BENCH / 'bin/fdkdec'))
AUDIO = BENCH / 'data/external/audio'


def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def run(cmd, env):
    return subprocess.run(cmd, check=True, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, env=env)


def clean_env(flags=None):
    env = {'PATH': os.environ.get('PATH', '/usr/bin:/bin'), 'LANG': 'C', 'LC_ALL': 'C'}
    for key in ('CCACHE_DISABLE', 'NUMBA_DISABLE_JIT'):
        if key in os.environ:
            env[key] = os.environ[key]
    env.update(flags or {})
    return env


def payload_sha(path, source_wav):
    result = run(['ffprobe', '-v', 'error', '-select_streams', 'a:0',
                  '-show_packets', '-show_data_hash', 'sha256',
                  '-show_entries', 'packet=size,data_hash', '-of', 'json', str(path)],
                 clean_env())
    packets = json.loads(result.stdout).get('packets', [])
    if not packets:
        raise RuntimeError(f'No AAC packets: {path}')
    h = hashlib.sha256()
    total = 0
    for packet in packets:
        size = int(packet['size'])
        total += size
        h.update(f"{size}:{packet['data_hash']}\n".encode())
    with wave.open(str(source_wav), 'rb') as source:
        duration = source.getnframes() / source.getframerate()
    return h.hexdigest(), total * 8.0 / duration / 1000.0


def profile(path):
    result = run(['ffprobe', '-v', 'error', '-select_streams', 'a:0',
                  '-show_entries', 'stream=profile', '-of', 'json', str(path)], clean_env())
    streams = json.loads(result.stdout).get('streams', [])
    return streams[0].get('profile', '') if len(streams) == 1 else ''


def encode(label, binary, clip, rate, flags, strict):
    source = AUDIO / clip
    target = OUT / label / str(rate) / f'{Path(clip).stem}.m4a'
    decoded = OUT / label / str(rate) / f'{Path(clip).stem}.fdk.wav'
    pcm = OUT / label / str(rate) / f'{Path(clip).stem}.pcm48.wav'
    target.parent.mkdir(parents=True, exist_ok=True)
    encoder = FAACEncoder('faac-098-adaptive-border', str(binary), profile='he')
    command = encoder.get_encode_cmd(str(source), str(target), rate, 2, 48000)
    if '--object-type' not in command or command[command.index('--object-type') + 1] != 'he-aac-v1':
        raise RuntimeError(f'Not forced HE-AAC v1: {command}')
    run(command, clean_env(flags))
    prof = profile(target)
    if prof != 'HE-AAC':
        raise RuntimeError(f'{label}|{clip}|{rate}: expected HE-AAC profile, got {prof}')
    fdk_env = clean_env(FDK.get_run_env())
    run(FDK.get_decode_cmd(str(target), str(decoded)), fdk_env)
    if not utils.wav_conv(str(decoded), str(pcm), rate=48000, channels=2):
        raise RuntimeError(f'FDK PCM conversion failed: {label}|{clip}|{rate}')
    packet, kbps = payload_sha(target, source)
    record = {
        'variant': label, 'clip': clip, 'target_kbps': rate,
        'source_wav': str(source), 'source_sha256': sha(source),
        'encoded_path': str(target), 'encoded_sha256': sha(target),
        'payload_sha256': packet, 'actual_payload_kbps': kbps,
        'profile': prof, 'fdk_path': str(pcm), 'decoded_sha256': sha(pcm),
        'environment': flags or {}, 'encode_command': command,
    }
    if strict:
        faad_out = target.with_suffix('.faad.wav')
        run([str(FAAD), '--strict', '--overwrite', '-o', str(faad_out), str(target)], clean_env())
        if not faad_out.is_file():
            raise RuntimeError(f'FAAD produced no output: {label}|{clip}|{rate}')
        faad_out.unlink()
        run([str(FFMPEG), '-v', 'error', '-xerror', '-i', str(target), '-f', 'null', '-'], clean_env())
        record['faad_strict_pass'] = True
        record['ffmpeg_strict_pass'] = True
    return record


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    rows = {}
    for clip in CLIPS:
        for rate in RATES:
            key = f'{clip}|{rate}'
            baseline = encode('baseline', BASE, clip, rate, {}, False)
            unset = encode('unset', PROBE, clip, rate, {}, False)
            match = (baseline['payload_sha256'] == unset['payload_sha256'] and
                     baseline['decoded_sha256'] == unset['decoded_sha256'])
            unset['baseline_packet_match'] = baseline['payload_sha256'] == unset['payload_sha256']
            unset['baseline_pcm_match'] = baseline['decoded_sha256'] == unset['decoded_sha256']
            if not match:
                raise RuntimeError(f'Unset control is not exact: {key}')
            row = {'baseline': baseline, 'unset': unset, 'adaptive': {}}
            for lam in ('0.001', '0.003'):
                label = f'lambda-{lam}'
                flags = {'FAAC_SBR_BORDER_MODE': '1', 'FAAC_SBR_BORDER_LAMBDA': lam}
                row['adaptive'][lam] = encode(label, PROBE, clip, rate, flags, True)
            rows[key] = row
            (OUT / 'gate.json').write_text(json.dumps({'schema': 1, 'rows': rows},
                                                       indent=2, sort_keys=True) + '\n')
            print(f'PASS {key} unset=packet+PCM exact; adaptive λ=.001/.003 FAAD+FFmpeg strict',
                  flush=True)
    summary = {
        'unset_packet_pcm_exact': sum(
            item['unset']['baseline_packet_match'] and item['unset']['baseline_pcm_match']
            for item in rows.values()),
        'expected_unset_rows': len(CLIPS) * len(RATES),
        'adaptive_rows': sum(len(item['adaptive']) for item in rows.values()),
        'faad_strict_pass': sum(
            x['faad_strict_pass'] for item in rows.values() for x in item['adaptive'].values()),
        'ffmpeg_strict_pass': sum(
            x['ffmpeg_strict_pass'] for item in rows.values() for x in item['adaptive'].values()),
    }
    (OUT / 'summary.json').write_text(json.dumps(summary, indent=2, sort_keys=True) + '\n')
    print(json.dumps(summary, sort_keys=True), flush=True)
    if summary['unset_packet_pcm_exact'] != 16 or summary['adaptive_rows'] != 32 or \
       summary['faad_strict_pass'] != 32 or summary['ffmpeg_strict_pass'] != 32:
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
