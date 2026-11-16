#!/usr/bin/env python3
"""Parameterized encode/decode/score harness using faac-benchmark (9f5f102) functions.
Row key: (arm, clip, profile, mode, rung, decoder). Resumable, appends rows per result."""
import argparse, hashlib, json, os, shutil, subprocess, sys, tempfile, wave
from multiprocessing import Pool

FB = os.environ.get('FB', '/home/user/work/fb')
AUDIO = os.environ.get('AUDIO', FB + '/data/external/audio')
APPLE = os.environ.get('APPLE_REF', '/home/user/faac/probe/ladder/ref/apple-48k')
MANIFEST = APPLE + '/manifest.json'
V_RATE, V_CH = 48000, 2  # benchmark: 48k_stereo scenarios (visqol_rate=48000 audio mode, 2 ch)
OT = {'lc': 'lc', 'he': 'he-aac-v1', 'hev2': 'he-aac-v2'}
FDKP = {'lc': '2', 'he': '5', 'hev2': '29'}
XAOT = {'lc': '2', 'he': '5', 'hev2': '29'}
CFG = {}  # filled in main (decoder bins, arms)


def _bench():
    if FB not in sys.path:
        sys.path.insert(0, FB)
    import utils, phase2_mos
    return utils, phase2_mos


def src_duration(p):
    with wave.open(p) as w:
        return w.getnframes() / w.getframerate()


def es_bits(path):
    r = subprocess.run(['ffprobe', '-v', 'error', '-select_streams', 'a:0', '-show_entries',
                        'packet=size', '-of', 'csv=p=0', path], capture_output=True, text=True, check=True)
    return 8 * sum(int(x) for x in r.stdout.replace(',', ' ').split())


def stream_rate(path):
    r = subprocess.run(['ffprobe', '-v', 'error', '-select_streams', 'a:0', '-show_entries',
                        'stream=sample_rate', '-of', 'csv=p=0', path], capture_output=True, text=True)
    try:
        return int(r.stdout.strip().split()[0])
    except Exception:
        return None


def encode_cmd(arm, clip_path, out, profile, mode, rung):
    k, b, fl = arm['kind'], arm['bin'], list(arm.get('flags', []))
    if k == 'faac':  # codec_bench.encoders.FAACEncoder.get_encode_cmd order
        cmd = [b]
        cmd += ['-q', str(rung)] if mode == 'vbr' else ['-b', str(rung)] + (['--cbr'] if mode == 'cbr' else [])
        cmd += ['--overwrite', '-o', out, '--object-type', OT[profile]] + fl + [clip_path]
    elif k == 'fdkaac':  # FDKAACEncoder: -p P -b bps (CBR default); VBR: -m <1..5> (not in benchmark)
        cmd = [b, '-p', FDKP[profile]] + (['-m', str(rung)] if mode == 'vbr' else ['-b', str(rung * 1000)])
        cmd += fl + ['-o', out, clip_path]
    elif k == 'xaac':  # IttiamAACEncoder
        cmd = [b, f'-ifile:{clip_path}', f'-ofile:{out}', f'-br:{rung * 1000}', f'-aot:{XAOT[profile]}', '-adts:1'] + fl
    else:
        raise ValueError(k)
    return cmd


def apple_path(clip, profile, rung):
    d = {'lc': 'lc', 'he': 'he', 'hev2': 'hev2'}[profile]
    return f'{APPLE}/{d}/{rung}k/{clip}.m4a'


def decode(dec, stream, raw, tmp):
    """Mirror phase2_mos.decode_bitstream (no silent ffmpeg fallback). Returns (ok, native_rate)."""
    utils, _ = _bench()
    if dec == 'ffmpeg':
        return True, stream_rate(stream)
    if dec == 'faad2':
        cmd = [CFG['faad2'], '-q', '-o', raw, stream]
    elif dec == 'faad3':
        cmd = [CFG['faad3'], '-q', '--strict', '-o', raw, stream]
    elif dec == 'fdkdec':
        cmd = [CFG['fdkdec'], stream, raw]
    else:
        raise ValueError(dec)
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0 or not os.path.exists(raw):
        return False, None
    try:
        with wave.open(raw) as w:
            return True, w.getframerate()
    except Exception:
        return True, None


def score(dec, stream, ref48, tmp, metrics):
    utils, p2 = _bench()
    out = {}
    raw = os.path.join(tmp, 'raw.wav')
    deg = os.path.join(tmp, 'deg.wav')
    ok, nat = decode(dec, stream, raw, tmp)
    out['decoded_rate'] = nat
    if not ok:
        out['error'] = 'decode_failed'
        return out
    src = stream if dec == 'ffmpeg' else raw
    if not utils._wav_conv_ffmpeg(src, deg, V_RATE, V_CH):  # same as wav_conv(.., rate, channels)
        out['error'] = 'wav_conv_failed'
        return out
    if 'mos' in metrics:
        mos, be = p2.score_wav_pair(ref48, deg, 'audio', sample_rate=V_RATE)
        out['mos'], out['mos_backend'] = mos, be
        if mos is None:
            out['error'] = 'score_failed'
    if 'nper' in metrics:
        try:
            import soundfile as sf, transient
            r, rs = sf.read(ref48, dtype='float32', always_2d=True)
            d, ds = sf.read(deg, dtype='float32', always_2d=True)
            pe = transient.pre_echo_metrics(r, d, rs)
            v = [max(0.0, float(x)) for x in pe['pre_echo_db']]
            out['nper'] = sum(v) / len(v) if v else None
            out['nper_events'] = len(v)
        except Exception as e:
            out['nper_error'] = str(e)
    if 'stfid' in metrics:
        try:
            import phase3_stereo
            e = phase3_stereo.coherence_error(ref48, deg)
            out['stfid'] = None if e is None else 1.0 - e
        except Exception as e:
            out['stfid_error'] = str(e)
    return out


def job(a):
    arm_name, clip, profile, mode, rung, decs, metrics, outroot = a
    arm = CFG['arms'][arm_name]
    utils, _ = _bench()
    base = dict(arm=arm_name, clip=clip, profile=profile, mode=mode, rung=rung)
    clip_path = os.path.join(CFG['audio'], clip)
    try:
        dur = src_duration(clip_path)
        d = f'{outroot}/{arm_name}/{profile}/{mode}/{rung}'
        os.makedirs(d, exist_ok=True)
        if arm['kind'] == 'apple':
            enc = apple_path(clip, profile, rung)
            if not os.path.exists(enc):
                return [dict(base, decoder=x, error='apple_stream_missing') for x in decs]
        else:
            enc = f'{d}/{clip}.m4a'
            if os.path.exists(enc):
                os.remove(enc)
            env = dict(os.environ, **arm.get('env', {}))
            r = subprocess.run(encode_cmd(arm, clip_path, enc, profile, mode, rung), capture_output=True, text=True, env=env)
            if r.returncode != 0 or not os.path.exists(enc):
                return [dict(base, decoder=x, error='encode_failed: ' + (r.stderr or '')[-200:]) for x in decs]
        stats = dict(bytes=os.path.getsize(enc), sha256=hashlib.sha256(open(enc, 'rb').read()).hexdigest(),
                     kbps_realized=es_bits(enc) / dur / 1000, src_duration=dur)
        ref48 = utils.get_cached_ref_wav(CFG['refcache'], clip_path, V_RATE, V_CH)
        rows = []
        for dec in decs:
            with tempfile.TemporaryDirectory(dir=CFG['tmp']) as tmp:
                try:
                    res = score(dec, enc, ref48, tmp, metrics)
                except Exception as e:
                    res = dict(error=f'exception: {e}')
            rows.append(dict(base, decoder=dec, **stats, **res))
        return rows
    except Exception as e:
        return [dict(base, decoder=x, error=f'exception: {e}') for x in decs]


def key(r):
    return (r['arm'], r['clip'], r['profile'], r['mode'], str(r['rung']), r['decoder'])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--arms', required=True)
    ap.add_argument('--arms-file', required=True)
    ap.add_argument('--clips', default=None, help='file with clip names, or comma list; default all manifest clips present')
    ap.add_argument('--rungs', required=True)
    ap.add_argument('--mode', default='abr', choices=['abr', 'cbr', 'vbr'])
    ap.add_argument('--profile', default='lc', choices=['lc', 'he', 'hev2'])
    ap.add_argument('--decoders', default='faad2,ffmpeg,fdkdec')
    ap.add_argument('--metrics', default='mos')
    ap.add_argument('--out', required=True)
    ap.add_argument('--rows', required=True)
    ap.add_argument('-j', type=int, default=4)
    ap.add_argument('--audio', default=AUDIO)
    ap.add_argument('--faad2', default=os.environ.get('FAAD', '/home/user/work/faad2-prefix/bin/faad'))
    ap.add_argument('--faad3', default=os.environ.get('FAAD3', '/home/user/work/faac-master/build/frontend/faad'))
    ap.add_argument('--fdkdec', default=os.environ.get('FDKDEC', FB + '/bin/fdkdec'))
    a = ap.parse_args()
    arms = json.load(open(a.arms_file))
    names = a.arms.split(',')
    for n in names:
        if n not in arms:
            sys.exit(f'arm {n} not in {a.arms_file}')
    if a.clips and os.path.exists(a.clips):
        clips = [l.strip() for l in open(a.clips) if l.strip()]
    elif a.clips:
        clips = a.clips.split(',')
    else:
        man = json.load(open(MANIFEST))
        clips = sorted({r['clip'] for r in man['rows']})
    clips = [c for c in clips if os.path.exists(os.path.join(a.audio, c))]
    rungs = [int(x) for x in a.rungs.split(',')]
    decs, metrics = a.decoders.split(','), set(a.metrics.split(','))
    os.makedirs(a.out, exist_ok=True)
    CFG.update(arms={n: arms[n] for n in names}, audio=a.audio, faad2=a.faad2, faad3=a.faad3, fdkdec=a.fdkdec,
               refcache=os.path.join(a.out, '_ref48'), tmp=os.path.join(a.out, '_tmp'))
    os.makedirs(CFG['refcache'], exist_ok=True); os.makedirs(CFG['tmp'], exist_ok=True)
    done = set()
    if os.path.exists(a.rows):
        for l in open(a.rows):
            try:
                r = json.loads(l)
            except Exception:
                continue
            if not r.get('error') and r.get('profile') == a.profile and r.get('mode') == a.mode:
                done.add(key(r))
    jobs = []
    for n in names:
        for rung in rungs:
            for c in clips:
                need = [d for d in decs if (n, c, a.profile, a.mode, str(rung), d) not in done]
                if need:
                    jobs.append((n, c, a.profile, a.mode, rung, need, metrics, a.out))
    print(f'{len(clips)} clips, {len(jobs)} jobs to run ({len(names)*len(rungs)*len(clips)} total)', flush=True)
    sys.path.insert(0, FB)
    with open(a.rows, 'a') as f, Pool(a.j) as p:
        for i, rows in enumerate(p.imap_unordered(job, jobs)):
            for r in rows:
                f.write(json.dumps(r) + '\n')
            f.flush()
            if i % 20 == 0:
                print(i, len(jobs), flush=True)
    print('done', flush=True)


if __name__ == '__main__':
    main()
