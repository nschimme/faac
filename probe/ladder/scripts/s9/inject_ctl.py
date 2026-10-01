#!/Users/nschimme/gitprojects/faac-benchmark/.venv/bin/python
"""Serial HE injector known-answer controls; exits nonzero on any failed gate."""
import argparse
import hashlib
import json
import os
import pathlib
import subprocess
import sys

import numpy as np
import soundfile as sf
from scipy import signal

ROOT = pathlib.Path(__file__).resolve().parents[4]
LADDER = ROOT / 'probe/ladder'
sys.path.insert(0, str(LADDER))
import parse_dump
AUDIO = pathlib.Path('/Users/nschimme/gitprojects/faac-benchmark/data/external/audio')
FAAC = ROOT / 'build_ladder/frontend/faac'
FAAD = pathlib.Path('/private/tmp/claude-501/faac-work/faad-dump/build-ladder/frontend/faad')
CONV = LADDER / 'scripts/h/h_conv.py'
BASE = dict(FAAC_SF_SMOOTH='0.6', FAAC_BS_DROPRATIO='12', FAAC_SBR_FREQ_SCALE='3')
CORE_SAMPLES = 2048
APPLE_HE48 = LADDER / 'ref/apple_he48k'
DEFAULT_ALIGNMENT = {'fdk': (2015, 0), 'apple': (96, 1)}

def run(cmd, env=None):
    return subprocess.run(list(map(str, cmd)), env=env, check=True, capture_output=True, text=True)

def encode(src, dst, ci=None, fields=None, offset=1):
    env = os.environ.copy()
    env.update(BASE)
    for name in ('FAAC_CORE_INJECT','FAAC_CORE_INJECT_FIELDS','FAAC_CORE_INJECT_OFFSET','FAAC_CORE_INJECT_LOOSE_SFB'):
        env.pop(name, None)
    if ci:
        env.update(FAAC_CORE_INJECT=str(ci), FAAC_CORE_INJECT_FIELDS=fields,
                   FAAC_CORE_INJECT_OFFSET=str(offset), FAAC_CORE_INJECT_LOOSE_SFB='1')
    run([FAAC, '--overwrite', '--object-type', 'he-aac-v1', '-b', '48', '-o', dst, src], env)

def dump(aac, txt, wav):
    txt.unlink(missing_ok=True)
    adts = txt.with_suffix('.adts')
    run(['ffmpeg', '-v', 'error', '-y', '-i', aac, '-c:a', 'copy', '-f', 'adts', adts])
    env = os.environ.copy()
    env.update(FAAD_DUMP=str(txt), FAAD_LADDER_DUMP='1')
    run([FAAD, '-o', wav, adts], env)

def pcm(aac, wav):
    run(['ffmpeg', '-v', 'error', '-y', '-i', aac, '-f', 'wav', wav])
    return hashlib.sha256(wav.read_bytes()).hexdigest()

def win_match(a, b, shift=0):
    x, y = parse_dump.parse(a), parse_dump.parse(b)
    hit = total = 0
    for f, chans in x.items():
        for ch, ics in chans.items():
            other = y.get(f + shift, {}).get(ch)
            if other is not None:
                total += 1
                hit += ics.win_seq == other.win_seq
    return hit / total if total else 0.0

def audio_lag(src, decoded):
    source, rate = sf.read(src)
    output, out_rate = sf.read(decoded)
    if rate != 48000 or out_rate != rate:
        raise ValueError(f'Expected 48 kHz PCM: {src}, {decoded}')
    if source.ndim == 2:
        source = source.mean(axis=1)
    if output.ndim == 2:
        output = output.mean(axis=1)
    b, a = signal.butter(6, 4000 / 24000)
    source = signal.filtfilt(b, a, source[:480000])
    output = signal.filtfilt(b, a, output[:496384])
    n = 1 << 21
    corr = np.fft.irfft(np.fft.rfft(output, n) * np.conj(np.fft.rfft(source, n)), n)
    return int(np.argmax(corr[:16384]))

def padded_wav(src, dst, pad):
    data, rate = sf.read(src)
    if pad >= 0:
        data = np.concatenate((np.zeros((pad,) + data.shape[1:]), data))
    else:
        data = data[-pad:]
    sf.write(dst, data, rate)

def alignment_control(src, t, base, base_dump, base_wav, fdk_binary):
    stem = src.stem
    result = {'faac_lag': audio_lag(src, base_wav), 'references': {}}
    hashes = {}
    for pad in (0, CORE_SAMPLES):
        padded = t / f'whole_frame_pad{pad}.wav'
        encoded = t / f'whole_frame_pad{pad}.m4a'
        padded_wav(src, padded, pad)
        encode(padded, encoded)
        hashes[str(pad)] = hashlib.md5(encoded.read_bytes()).hexdigest()
    result['whole_frame_encode_md5'] = hashes
    result['whole_frame_changes_encode'] = hashes['0'] != hashes[str(CORE_SAMPLES)]
    zero_dump = t / 'whole_frame_pad0.dump'
    shifted_dump = t / f'whole_frame_pad{CORE_SAMPLES}.dump'
    dump(t / 'whole_frame_pad0.m4a', zero_dump, t / 'whole_frame_pad0.wav')
    dump(t / f'whole_frame_pad{CORE_SAMPLES}.m4a', shifted_dump,
         t / f'whole_frame_pad{CORE_SAMPLES}.wav')
    result['whole_frame_match_fixed'] = win_match(zero_dump, shifted_dump, 0)
    result['whole_frame_match_shifted'] = win_match(zero_dump, shifted_dump, 1)

    for name in ('fdk', 'apple'):
        if name == 'fdk':
            reference = t / 'fdk.m4a'
            run([fdk_binary, '-S', '-p', '5', '-b', '48000', '-o', reference, src])
        else:
            reference = APPLE_HE48 / (stem + '.m4a')
        if not reference.exists():
            raise FileNotFoundError(reference)
        probe = run(['ffprobe', '-v', 'error', '-select_streams', 'a:0',
                     '-show_entries', 'stream=profile', '-of', 'default=noprint_wrappers=1',
                     reference]).stdout
        if 'profile=HE-AAC' not in probe:
            raise ValueError(f'{name} reference is not HE-AAC: {reference}: {probe.strip()}')
        ref_dump = t / f'{name}.dump'
        ref_wav = t / f'{name}.wav'
        dump(reference, ref_dump, ref_wav)
        reference_lag = audio_lag(src, ref_wav)
        difference = reference_lag - result['faac_lag']
        derived_pad = difference % CORE_SAMPLES
        derived_offset = (difference - derived_pad) // CORE_SAMPLES
        pad, frame_offset = DEFAULT_ALIGNMENT[name]
        grid = {}
        center_input = None
        for pad_delta in (-1024, 0, 1024):
            actual_pad = pad + pad_delta
            padded = t / f'{name}_pad{actual_pad}.wav'
            encoded = t / f'{name}_pad{actual_pad}.m4a'
            padded_wav(src, padded, actual_pad)
            encode(padded, encoded)
            decoded_dump = t / f'{name}_pad{actual_pad}.dump'
            dump(encoded, decoded_dump, t / f'{name}_pad{actual_pad}_decoded.wav')
            grid[str(pad_delta)] = {
                str(offset_delta): win_match(decoded_dump, ref_dump, frame_offset + offset_delta)
                for offset_delta in (-1, 0, 1)
            }
            if pad_delta == 0:
                center_input = padded
        center = grid['0']['0']
        neighbors = [grid['-1024']['0'], grid['1024']['0'],
                     grid['0']['-1'], grid['0']['1']]
        ci = t / f'{name}.ci'
        run([sys.executable, CONV, ref_dump, ci])
        injected = t / f'{name}_win_injected.m4a'
        encode(center_input, injected, ci, 'win', frame_offset + 1)
        injected_dump = t / f'{name}_win_injected.dump'
        dump(injected, injected_dump, t / f'{name}_win_injected.wav')
        result['references'][name] = {
            'profile': probe.strip(), 'lag': reference_lag, 'pad': pad, 'frame_offset': frame_offset,
            'audio_alignment': [derived_pad, derived_offset],
            'lag_alignment_matches_default': (derived_pad, derived_offset) == (pad, frame_offset),
            'inject_offset': frame_offset + 1, 'window_match': grid,
            'peak': all(center > neighbor for neighbor in neighbors),
            'injected_window_match': win_match(injected_dump, ref_dump, frame_offset),
        }
    return result

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--limit', type=int, default=3)
    p.add_argument('--out', type=pathlib.Path, default=LADDER / 'results/s9/controls')
    p.add_argument('--fdk', default='fdkaac')
    p.add_argument('--alignment-only', action='store_true')
    p.add_argument('--clips', help='Comma-separated clip stems for a focused control')
    args = p.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    clips = sorted(AUDIO.glob('*.wav'), key=lambda p: p.stat().st_size)
    if args.clips:
        selected = set(args.clips.split(','))
        clips = [src for src in clips if src.stem in selected]
        if len(clips) != len(selected):
            raise ValueError(f'Missing clip stems: {selected - {src.stem for src in clips}}')
    clips = clips[:args.limit]
    results = {}
    for src in clips:
        stem = src.stem
        t = args.out / stem
        t.mkdir(exist_ok=True)
        base = t / 'base.m4a'
        if args.alignment_only:
            encode(src, base)
            dump(base, t / 'base.dump', t / 'base_faad.wav')
            results[src.name] = alignment_control(src, t, base, t / 'base.dump',
                                                   t / 'base_faad.wav', args.fdk)
            print(src.name, json.dumps(results[src.name]), flush=True)
            (args.out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
            continue
        baseline_no_inj = t / 'baseline_no_inj.m4a'
        encode(src, base)
        encode(src, baseline_no_inj)
        base_digest = pcm(base, t / 'base_pcm.wav')
        dump(base, t / 'base.dump', t / 'base_faad.wav')
        run([sys.executable, CONV, t / 'base.dump', t / 'base.ci'])
        rec = {'unset_identical': base.read_bytes() == baseline_no_inj.read_bytes(), 'single': {}}
        for field in ('win', 'cls', 'sf', 'ms', 'tns', 'win,cls,sf,ms,tns'):
            name = field.replace(',', '_')
            dst = t / f'{name}.m4a'
            encode(src, dst, t / 'base.ci', field)
            same = pcm(dst, t / f'{name}.wav') == base_digest
            if ',' in field: rec['full_identical'] = same
            else: rec['single'][field] = same
        shifted = t / 'shifted.m4a'
        encode(src, shifted, t / 'base.ci', 'win', 2)
        pcm(shifted, t / 'shifted.wav')
        dump(shifted, t / 'shifted.dump', t / 'shifted_faad.wav')
        rec['shift_identical'] = (t / 'shifted.wav').read_bytes() == (t / 'base_pcm.wav').read_bytes()
        rec['shift_window_match'] = win_match(t / 'shifted.dump', t / 'base.dump')
        rec['self_window_match'] = win_match(t / 'base.dump', t / 'base.dump')
        rec['alignment'] = alignment_control(src, t, base, t / 'base.dump',
                                             t / 'base_faad.wav', args.fdk)
        results[src.name] = rec
        print(src.name, json.dumps(rec), flush=True)
        (args.out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    if args.alignment_only:
        good = all(x['whole_frame_changes_encode'] and x['whole_frame_match_shifted'] > .99
                   and all(ref['lag_alignment_matches_default']
                           and ref['injected_window_match'] >= .95
                           for ref in x['references'].values()) for x in results.values())
    else:
        good = all(x['unset_identical'] and x['full_identical'] and all(x['single'].values())
                   and not x['shift_identical'] and x['shift_window_match'] < .8
                   and all(ref['lag_alignment_matches_default']
                           and ref['injected_window_match'] >= .95
                           for ref in x['alignment']['references'].values())
                   for x in results.values())
    print('known-answer gates', 'PASS' if good else 'FAIL', flush=True)
    return 0 if good else 1

if __name__ == '__main__':
    sys.exit(main())
