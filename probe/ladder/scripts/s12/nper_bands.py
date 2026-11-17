#!/usr/bin/env python3
"""Band-resolved NPER for HE-v1 streams. Events fixed from the benchmark's (2 kHz HP) pipeline;
per-event pre-echo recomputed on 2-6 / 6-12 / 12-24 kHz zero-phase 8th-order Butterworth bands.
Decode path mirrors harness.py (faad -q -o raw; ffmpeg -> 48k stereo s16; float32 read)."""
import json, os, subprocess, sys, tempfile, glob
import numpy as np, scipy.signal as ss
FB = os.environ.get('FB', '/home/user/work/fb'); sys.path.insert(0, FB)
import transient as tm, utils, soundfile as sf
AUDIO = os.environ.get('AUDIO', FB + '/data/external/audio')
FAAD = os.environ.get('FAAD', '/home/user/work/faad2-prefix/bin/faad')
LAD = '/home/user/faac/probe/ladder'
STREAMS = {'faac': '/home/user/work/out/faac/he/cbr/{r}/{c}.m4a',
           'fdk': LAD + '/ref/fdk-48k/he/{r}k/{c}.m4a',
           'apple': LAD + '/ref/apple-48k/he/{r}k/{c}.m4a'}
RUNGS = [32, 40, 48]
BANDS = {'2-6k': ('bandpass', [2000, 6000], 4), '6-12k': ('bandpass', [6000, 12000], 4),
         '12-24k': ('highpass', 12000, 8)}   # bandpass N=4 -> 8th order overall
SOS = {k: ss.butter(n, w, btype=t, fs=48000, output='sos') for k, (t, w, n) in BANDS.items()}
OUT = '/home/user/faac/probe/ladder/results/s12-apple48k/decisions'
CACHE = '/home/user/work/nb/rows.jsonl'
REFC = '/home/user/work/nb/ref48'; os.makedirs(REFC, exist_ok=True)

def decode(stream, tmp):
    raw, deg = tmp + '/raw.wav', tmp + '/deg.wav'
    r = subprocess.run([FAAD, '-q', '-o', raw, stream], capture_output=True)
    if r.returncode or not os.path.exists(raw): return None
    if not utils._wav_conv_ffmpeg(raw, deg, 48000, 2): return None
    return deg

def band_db(refb, decb, ev):
    out = []
    for (ch, onset, dec_onset, pre_lo, pre_hi, dec_lo, _, _) in ev:
        post_hi = onset + tm.PRE_ECHO_POST_END
        rp = float(np.mean(refb[pre_lo:pre_hi, ch] ** 2)); rq = float(np.mean(refb[onset:post_hi, ch] ** 2))
        if rq < 1e-12: out.append(None); continue
        dp = float(np.mean(decb[dec_lo:dec_lo + (pre_hi - pre_lo), ch] ** 2))
        fl = rq * 1e-8
        out.append(float(10 * np.log10(max(dp, fl) / max(rp, fl))))
    return out

def run_one(enc, rung, clip):
    ref48p = utils.get_cached_ref_wav(REFC, os.path.join(AUDIO, clip), 48000, 2)
    with tempfile.TemporaryDirectory(dir='/home/user/work/nb') as tmp:
        deg = decode(STREAMS[enc].format(r=rung, c=clip), tmp)
        if deg is None: return None
        ref, rs = sf.read(ref48p, dtype='float32', always_2d=True)
        dec, ds = sf.read(deg, dtype='float32', always_2d=True)
    pe = tm.pre_echo_metrics(ref, dec, rs)          # reference full-band numbers
    ref = tm._audio_channels(ref, 'r'); dec = tm._audio_channels(dec, 'd')
    ref48 = tm._resample_audio(ref, rs); dec48 = tm._resample_audio(dec, rs)
    lag = tm._pre_echo_lag(ref48, dec48)
    assert lag == pe['pre_echo_lag_samples']
    sos = ss.butter(4, 2000, btype='highpass', fs=48000, output='sos')
    rh, dh = ss.sosfilt(sos, ref48, axis=0), ss.sosfilt(sos, dec48, axis=0)
    cov = {'candidates': 0, 'refined': 0, 'eligible': 0, 'scored': 0}
    ev = list(tm._scored_pre_echo_events(ref48, rh, dh, lag, cov))
    full = band_db(rh, dh, ev)
    assert len(full) == len(pe['pre_echo_db'])
    res = {'enc': enc, 'rung': rung, 'clip': clip, 'full_bench': pe['pre_echo_db'], 'full': full}
    for b, s in SOS.items():
        rb, db = ss.sosfiltfilt(s, ref48, axis=0), ss.sosfiltfilt(s, dec48, axis=0)
        res[b] = band_db(rb, db, ev)
    return res

def load():
    rows = {}
    if os.path.exists(CACHE):
        for l in open(CACHE):
            r = json.loads(l); rows[(r['enc'], r['rung'], r['clip'])] = r
    return rows

def collect():
    clips = sorted(os.path.basename(p) for p in glob.glob(AUDIO + '/*.wav'))
    rows = load()
    with open(CACHE, 'a') as f:
        for rung in RUNGS:
            for enc in STREAMS:
                for c in clips:
                    if (enc, rung, c) in rows: continue
                    if not os.path.exists(STREAMS[enc].format(r=rung, c=c)): continue
                    r = run_one(enc, rung, c)
                    if r is None: continue
                    f.write(json.dumps(r) + '\n'); f.flush(); rows[(enc, rung, c)] = r
            print('rung done', rung, flush=True)
    return rows

def summarize(rows):
    pos = lambda x: max(0.0, x)
    S = {}
    for enc in STREAMS:
        for rung in RUNGS:
            rs = [r for (e, g, c), r in rows.items() if e == enc and g == rung]
            if not rs: continue
            full_ev = [x for r in rs for x in r['full']]
            bench_ev = [x for r in rs for x in r['full_bench']]
            d = {'clips': len(rs), 'events': len(full_ev),
                 'full_pooled': float(np.mean([pos(x) for x in bench_ev])),
                 'full_recomputed_pooled': float(np.mean([pos(x) for x in full_ev])),
                 'full_clipmean': float(np.mean([np.mean([pos(x) for x in r['full_bench']]) for r in rs if r['full_bench']])),
                 'bands': {}}
            for b in BANDS:
                v = [x for r in rs for x in r[b]]
                ok = [(pos(x), pos(f)) for x, f in zip(v, full_ev) if x is not None]
                d['bands'][b] = {'excess_db': float(np.mean([a for a, _ in ok])),
                                 'dominant_share': float(np.mean([a > f - 3 for a, f in ok])),
                                 'frac_events_positive': float(np.mean([a > 0 for a, _ in ok])), 'n': len(ok)}
            S[f'{enc}/{rung}'] = d
    return S

def report(S):
    json.dump(S, open(OUT + '/he_nper_bands.json', 'w'), indent=1)
    L = ['# HE-v1 NPER by frequency band', '',
         'Events fixed from the benchmark pipeline (2 kHz HP, 4th order); pre-echo (-6..-1 ms vs ref pre, floor ref_post*1e-8) recomputed per band on',
         'zero-phase 8th-order Butterworth copies of ref and decoded (same filter). Excess = mean of max(0, dB) pooled over all events (all clips, channels).',
         'Dominance = share of events whose band excess > full-band excess - 3 dB (bands overlap in share; not exclusive).', '',
         '| enc | rung | events | full (bench) | full (recomp) | 2-6k excess / dom | 6-12k excess / dom | 12-24k excess / dom |', '|---|---|---|---|---|---|---|---|']
    for k, d in S.items():
        e, r = k.split('/')
        cells = ' | '.join(f"{d['bands'][b]['excess_db']:.2f} / {d['bands'][b]['dominant_share']*100:.0f}%" for b in BANDS)
        L.append(f"| {e} | {r}k | {d['events']} | {d['full_pooled']:.2f} | {d['full_recomputed_pooled']:.2f} | {cells} |")
    L += ['', 'Clip-mean full-band NPER: ' + ', '.join(f"{k}={d['full_clipmean']:.2f}" for k, d in S.items())]
    xo = OUT + '/he_nper_xover.md'
    if os.path.exists(xo): L += ['', open(xo).read()]
    open(OUT + '/he_nper_bands.md', 'w').write('\n'.join(L) + '\n')

def xover(nclips=10):
    """SBR start (crossover) from decoder dump S records: k0 = startMin+offset (FAAD2 sbr_fbt.c), Hz = k0*fs_out/128."""
    from collections import Counter
    smin = {48000: 11, 32000: 16, 44100: 12, 24000: 24, 16000: 24}
    offt = {48000: [-4, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 9, 11, 13, 16, 20], 32000: [-6, -4, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 9, 11, 13, 16]}
    smin = {48000: 11, 32000: 16}
    clips = sorted(os.path.basename(p) for p in glob.glob(AUDIO + '/*.wav'))[::max(1, 77 // nclips)][:nclips]
    L = ['## SBR start frequency (crossover) from decoder dumps', '', '| enc | rung | out rate | start_freq idx (mode, hist) | k0 | crossover Hz |', '|---|---|---|---|---|---|']
    out = {}
    for enc in STREAMS:
        for rung in RUNGS:
            cnt, rate = Counter(), None
            for c in clips:
                p = STREAMS[enc].format(r=rung, c=c)
                if not os.path.exists(p): continue
                d = tempfile.mktemp(dir='/home/user/work/nb')
                subprocess.run(['sh', os.path.dirname(os.path.abspath(__file__)) + '/decdump.sh', p, d], capture_output=True)
                if not os.path.exists(d): continue
                for l in open(d):
                    t = l.split()
                    if t[0] == 'R': rate = int(t[1]) * 2
                    elif t[0] == 'S': cnt[int(t[4])] += 1
                os.remove(d)
            if not cnt or rate not in smin: L.append(f'| {enc} | {rung}k | {rate} | none |  |  |'); continue
            idx = cnt.most_common(1)[0][0]; k0 = smin[rate] + offt[rate][idx]
            hz = k0 * rate / 128
            out[f'{enc}/{rung}'] = {'rate': rate, 'start_freq_hist': dict(cnt), 'k0': k0, 'xover_hz': hz}
            L.append(f'| {enc} | {rung}k | {rate} | {idx} ({dict(cnt.most_common(4))}) | {k0} | {hz:.0f} |')
    open(OUT + '/he_nper_xover.md', 'w').write('\n'.join(L) + '\n')
    json.dump(out, open(OUT + '/he_nper_xover.json', 'w'), indent=1)
    print('\n'.join(L))

if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == 'xover':
        xover(); sys.exit()
    if len(sys.argv) > 1 and sys.argv[1] == 'report':
        report(summarize(load()))
    else:
        rows = collect(); report(summarize(rows))
