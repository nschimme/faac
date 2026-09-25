#!/usr/bin/env python3
"""Reproducible forced-profile ladder; one suite at a time, parallel clips.

Run with the benchmark repository's Python environment. Each output directory
is immutable with respect to configuration; interrupted runs can be resumed.
Both ViSQOL and Zimtohrli are recorded explicitly, with both decoders.
"""
import argparse
import concurrent.futures as cf
import hashlib
import importlib.metadata
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import tempfile
import time

FAST = ['sandman.16b48k.wav', 'velvet.16b48k.wav', '21-classic.441.16b48k.wav',
        'fms.wav', 'Girl_In_The_Fire__Sample_.16b48k.wav',
        'Jupiter, the Bringer of Jolity.16b48k.wav',
        '35_SQAM_glockenspiel_cut.16b48k.wav', 'Hotel California.16b48k.wav']

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def run(cmd, **kw):
    p = subprocess.run(list(map(str, cmd)), capture_output=True, text=True, **kw)
    if p.returncode or 'err 0x' in p.stderr:
        raise RuntimeError(f'{cmd}: {p.stderr[-2000:]}')
    return p.stdout

def coherence(ref, dec, high=False):
    import numpy as np
    from scipy.signal import butter, sosfilt
    from phase3_stereo import coherence_vectorized, FRAME
    if ref.ndim != 2 or np.array_equal(ref[:, 0], ref[:, 1]):
        return None
    n = min(len(ref), len(dec)); ref = ref[:n]; dec = dec[:n]
    if high:
        hp = butter(4, 1500, 'hp', fs=48000, output='sos')
        ref = sosfilt(hp, ref, axis=0); dec = sosfilt(hp, dec, axis=0)
    r = coherence_vectorized(ref[:, 0], ref[:, 1], FRAME)
    d = coherence_vectorized(dec[:, 0], dec[:, 1], FRAME)
    energy = np.sum(ref**2, axis=1)[:len(r)*FRAME].reshape(len(r), FRAME).sum(1)
    keep = energy > 1e-3 * energy.max()
    return float(np.mean(np.abs(r-d)[keep])) if keep.any() else None

_api = None
_zim = None
def scores(refpath, decpath):
    global _api, _zim
    import numpy as np
    import soundfile as sf
    import scipy.signal as ss
    import zimtohrli
    from visqol import VisqolApi
    if _api is None:
        _api = VisqolApi(); _api.create(mode='audio')
        _zim = zimtohrli.Pyohrli()
    vis = float(_api.measure(str(refpath), str(decpath)).moslqo)
    ref, rr = sf.read(refpath, dtype='float32', always_2d=True)
    dec, dr = sf.read(decpath, dtype='float32', always_2d=True)
    assert rr == dr == 48000 and ref.shape[1] == dec.shape[1] == 2
    n = min(len(ref), len(dec), 3*48000)
    r = ref[:n].mean(axis=1); d = dec[:n].mean(axis=1)
    lag = int(np.argmax(ss.correlate(r/(np.std(r)+1e-10), d/(np.std(d)+1e-10))))-(n-1)
    if lag < 0: dec = dec[-lag:]; ref = ref[:len(ref)+lag]
    elif lag > 0: ref = ref[lag:]; dec = dec[:len(dec)-lag]
    n = min(len(ref), len(dec)); ref = ref[:n]; dec = dec[:n]
    distances = [_zim.distance(np.ascontiguousarray(ref[:, ch]), np.ascontiguousarray(dec[:, ch])) for ch in range(2)]
    z = float(zimtohrli.mos_from_zimtohrli(sum(x*x for x in distances)**0.5))
    return dict(visqol=vis, zimtohrli=z, ic=coherence(ref, dec), ichi=coherence(ref, dec, True), lag=lag)

def job(arg):
    cfg, arm, profile, rate, clip = arg
    sys.path.insert(0, cfg['benchmark'])
    spec = cfg['arms'][arm]
    out = Path(cfg['output']) / f'{profile}{rate}' / arm
    out.mkdir(parents=True, exist_ok=True)
    rowpath = out / (Path(clip).stem + '.json')
    if rowpath.exists(): return json.loads(rowpath.read_text())
    stream = out / (Path(clip).stem + '.m4a')
    if spec['kind'] == 'faac':
        cmd = [spec['binary'], '--overwrite', '--object-type', 'he-aac-v1' if profile == 'he' else 'lc', '-b', str(rate), *spec.get('args', []), '-o', str(stream), clip]
    elif spec['kind'] == 'fdk':
        cmd = ['fdkaac', '-p', '5' if profile == 'he' else '2', '-b', str(rate*1000), '-o', str(stream), clip]
    else:
        cmd = ['afconvert', '-f', 'm4af', '-d', 'aach' if profile == 'he' else 'aac', '-b', str(rate*1000), '-c', '2', clip, str(stream)]
    start = time.monotonic()
    run(cmd, env=dict(os.environ, **spec.get('env', {})))
    enc_seconds = time.monotonic()-start
    probe = json.loads(run(['ffprobe', '-v', 'error', '-show_streams', '-show_packets', '-show_entries', 'stream=profile,sample_rate,channels:packet=size', '-of', 'json', stream]))
    info = probe['streams'][0]
    assert info['profile'] == ('HE-AAC' if profile == 'he' else 'LC'), info
    row = dict(arm=arm, profile=profile, rate=rate, clip=Path(clip).name,
               bytes=stream.stat().st_size, payload_bytes=sum(int(p['size']) for p in probe['packets']),
               stream_sha256=sha(stream), stream_info=info, encode_seconds=enc_seconds, command=cmd, env=spec.get('env', {}))
    with tempfile.TemporaryDirectory() as tmp:
        for decoder in ['ffmpeg', 'fdk']:
            wav = Path(tmp) / (decoder+'.wav')
            if decoder == 'ffmpeg':
                run(['ffmpeg', '-v', 'error', '-y', '-i', stream, '-ar', '48000', '-ac', '2', wav])
            else:
                raw = Path(tmp)/'raw.wav'
                run([Path(cfg['benchmark'])/'bin/fdkdec', stream, raw])
                run(['ffmpeg', '-v', 'error', '-y', '-i', raw, '-ar', '48000', '-ac', '2', wav])
            row[decoder] = scores(clip, wav)
    rowpath.write_text(json.dumps(row, indent=2)+'\n')
    return row

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('config'); ap.add_argument('-j', type=int, default=6)
    ap.add_argument('--fast', action='store_true'); ap.add_argument('--rates', default='he40,he48,he64,lc96,lc128,lc160,lc192')
    a = ap.parse_args(); cfg = json.loads(Path(a.config).read_text())
    output = Path(cfg['output']); output.mkdir(parents=True, exist_ok=True)
    os.environ['NUMBA_CACHE_DIR'] = str(output/'numba')
    os.environ['NUMBA_NUM_THREADS'] = '1'; os.environ['OMP_NUM_THREADS'] = '1'; os.environ['OPENBLAS_NUM_THREADS'] = '1'
    corpus = Path(cfg['benchmark'])/'data/external/audio'
    clips = [corpus/n for n in FAST] if a.fast else sorted(corpus.glob('*.wav'))
    assert len(clips) == (8 if a.fast else 49)
    metadata = dict(config=cfg, clips={p.name: sha(p) for p in clips}, rates=a.rates,
        packages={p: importlib.metadata.version(p) for p in ['visqol-python','zimtohrli','numpy','scipy','soundfile']},
        platform=platform.platform(), benchmark_sha=run(['git','-C',cfg['benchmark'],'rev-parse','HEAD']).strip(),
        ffmpeg=run(['ffmpeg','-version']).splitlines()[0], fdkdec_sha=sha(Path(cfg['benchmark'])/'bin/fdkdec'),
        fdkaac=subprocess.run(['fdkaac','--version'],capture_output=True,text=True).stdout.strip(),
        apple_os=run(['sw_vers']), arms={})
    for name, arm in cfg['arms'].items():
        if arm['kind'] != 'faac': continue
        binary = Path(arm['binary']); build = binary.parent.parent; tree = build.parent
        metadata['arms'][name] = dict(binary_sha=sha(binary), library_sha=sha(build/'libfaac/libfaac.2.dylib'),
            source_sha=run(['git','-C',tree,'rev-parse','HEAD']).strip(),
            diff_sha=hashlib.sha256(run(['git','-C',tree,'diff','HEAD']).encode()).hexdigest(),
            build=json.loads((build/'meson-info/intro-buildoptions.json').read_text()))
    manifest = output/'manifest.json'
    if manifest.exists(): assert json.loads(manifest.read_text()) == metadata, 'Provenance changed; use a fresh output directory'
    else: manifest.write_text(json.dumps(metadata, indent=2)+'\n')
    jobs = [(cfg, arm, rate[:2], int(rate[2:]), str(clip)) for rate in a.rates.split(',') for arm in cfg['arms'] for clip in clips]
    with cf.ProcessPoolExecutor(a.j) as pool:
        for i, row in enumerate(pool.map(job, jobs), 1):
            print(f"{i}/{len(jobs)} {row['profile']}{row['rate']} {row['arm']} {row['clip']} V={row['ffmpeg']['visqol']:.4f} Z={row['ffmpeg']['zimtohrli']:.4f}", flush=True)

if __name__ == '__main__': main()
