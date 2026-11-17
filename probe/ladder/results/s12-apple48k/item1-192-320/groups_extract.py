#!/usr/bin/env python3
"""Per-clip/rung decision features (FAAC vs Apple) + source descriptors. Single process."""
import os, sys, json, subprocess, tempfile, numpy as np, soundfile as sf
sys.path.insert(0, '/home/user/faac/probe/ladder/scripts/s12')
R = '/home/user/faac/probe/ladder/results/s12-apple48k/stage1-rebaseline/'
OUT = os.path.dirname(os.path.abspath(__file__)) + '/'
AUDIO = os.environ['AUDIO']
REG = [(0, 6000), (6000, 12000), (12000, 16000), (16000, 20000)]
RN = ['0-6', '6-12', '12-16', '16-20']

def dump(stream):
    f = tempfile.NamedTemporaryFile(dir='/tmp', suffix='.dd', delete=False); f.close()
    subprocess.run(['nice', '-n', '10', '/home/user/faac/probe/ladder/scripts/s12/decdump.sh', stream, f.name], check=True)
    return f.name

def feats(path):
    rate = 48000; fl = 1024
    ics = {}; nics = nshort = ntns = 0
    bw = []; frame_bw = {}
    zl = np.zeros(4); tl = np.zeros(4)  # zero lines / total lines (long)
    sfs = [[] for _ in RN]
    n_coded6 = n_pns6 = n_coded = n_is = 0; n_ms = n_msden = 0
    cur = {}
    with open(path) as fh:
        for ln in fh:
            t = ln.split()
            if t[0] == 'R': rate = int(t[1]); fl = int(t[2])
            elif t[0] == 'I':
                fr, ch, ws, tns = int(t[1]), int(t[2]), int(t[3]), int(t[9])
                nics += 1; nshort += ws == 2; ntns += tns
                cur = {'ws': ws}
                if ws != 2:
                    cur['lines'] = np.ones(fl, bool)  # True = zero
                    cur['hi'] = 0.0; cur['key'] = (fr, ch)
                    cur['mine'] = cur
                    ics[(fr, ch)] = cur
            elif t[0] == 'B':
                if cur.get('ws', 2) == 2: continue
                s, e, cb, sf_, ms = int(t[5]), int(t[6]), int(t[7]), int(t[8]), int(t[9])
                ch = int(t[2])
                hz = lambda x: x * rate / 2 / fl
                fc = (hz(s) + hz(e)) / 2
                if cb != 0:
                    cur['lines'][s:e] = False
                    n_coded += 1; n_is += cb in (14, 15)
                    if cb <= 11:
                        cur['hi'] = max(cur['hi'], hz(e))
                        for i, (lo, hi) in enumerate(REG):
                            if lo <= fc < hi: sfs[i].append(sf_)
                    if fc >= 6000:
                        n_coded6 += 1; n_pns6 += cb == 13
                    if ch == 0:
                        n_msden += 1; n_ms += ms != 0
    frames = {}
    for (fr, ch), c in ics.items():
        frames[fr] = max(frames.get(fr, 0.0), c['hi'])
        z = c['lines']
        for i, (lo, hi) in enumerate(REG):
            a, b = int(lo / (rate / 2) * fl), int(min(hi, rate / 2) / (rate / 2) * fl)
            zl[i] += z[a:b].sum(); tl[i] += b - a
    r = {}
    r['bw_hz'] = float(np.percentile(list(frames.values()), 95)) if frames else np.nan
    r['pns_share_6k'] = n_pns6 / n_coded6 if n_coded6 else 0.0
    r['is_share'] = n_is / n_coded if n_coded else 0.0
    r['ms_share'] = n_ms / n_msden if n_msden else 0.0
    for i, n in enumerate(RN):
        r['zero_' + n] = zl[i] / tl[i] if tl[i] else np.nan
        r['sf_' + n] = float(np.mean(sfs[i])) if sfs[i] else np.nan
    r['short_share'] = nshort / nics if nics else np.nan
    r['tns_share'] = ntns / nics if nics else np.nan
    return r

def src(wav):
    from ref_onsets import onsets
    x, sr = sf.read(wav, dtype='float64', always_2d=True)
    m = x.mean(1)
    # PSD (Welch-like, mean of mono-ish both channels)
    from scipy.signal import welch
    f, p = welch(x, fs=sr, nperseg=4096, axis=0); p = p.sum(1)
    tot = p.sum()
    d = {'e12k': p[f >= 12000].sum() / tot, 'e16k': p[f >= 16000].sum() / tot}
    d['rms_db'] = 20 * np.log10(np.sqrt((x ** 2).mean()) + 1e-12)
    if x.shape[1] >= 2 and x[:, 0].std() > 0 and x[:, 1].std() > 0:
        d['lr_corr'] = float(np.corrcoef(x[:, 0], x[:, 1])[0, 1])
    else: d['lr_corr'] = 1.0
    ev, cov, _, n = onsets(wav)
    d['onsets_per_s'] = len(ev) / x.shape[1] / (len(x) / sr)
    return d

if __name__ == '__main__':
    worse = [l.strip() for l in open(R + 'lc_abr/g192_worse.txt') if l.strip()]
    better = [l.strip() for l in open(R + 'lc_abr/g192_better.txt') if l.strip()]
    bd = json.load(open(R + 'lc_abr/faac_vs_apple_faad2_192-256-320_all.json'))['per_clip_lin']
    kb = {}
    for l in open(R + 'rows_lc_abr.jsonl'):
        j = json.loads(l)
        if j['decoder'] == 'faad2' and j['rung'] in (192, 256, 320):
            kb[(j['arm'], j['clip'], j['rung'])] = j['kbps_realized']
    cache = OUT + 'cache.json'
    C = json.load(open(cache)) if os.path.exists(cache) else {'rows': {}, 'src': {}}
    clips = [(c, 'worse') for c in worse] + [(c, 'better') for c in better]
    for c, g in clips:
        if c not in C['src']:
            C['src'][c] = src(f'{AUDIO}/{c}')
        for rung in (192, 256, 320):
            for arm in ('faac', 'apple'):
                k = f'{arm}|{c}|{rung}'
                if k in C['rows']: continue
                s = f'/home/user/work/out/faac/lc/abr/{rung}/{c}.m4a' if arm == 'faac' else f'/home/user/faac/probe/ladder/ref/apple-48k/lc/{rung}k/{c}.m4a'
                d = dump(s)
                try: r = feats(d)
                finally: os.unlink(d)
                r['kbps'] = kb.get((arm, c, rung), np.nan)
                C['rows'][k] = r
        json.dump(C, open(cache, 'w'))
        print(c, flush=True)
