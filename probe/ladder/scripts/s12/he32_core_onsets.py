#!/usr/bin/env python3
"""HE-v1 32k core decisions at scored onsets: FAAC vs FDK vs Apple (decoder dump), joined with FAAC per-event band pre-echo.
usage: he32_core_onsets.py extract | report"""
import os, sys, json, glob, subprocess, pickle, tempfile, collections
import numpy as np
from scipy.stats import spearmanr
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
LAD = '/home/user/faac/probe/ladder'
AUDIO = os.environ.get('AUDIO', '/home/user/work/fb/data/external/audio')
FAAD = os.environ.get('FAAD', '/home/user/work/faad2-prefix/bin/faad')
W = '/home/user/work/he32core'; os.makedirs(W, exist_ok=True)
OUT = LAD + '/results/s12-apple48k/decisions'
STREAMS = {'faac': '/home/user/work/out/faac/he/cbr/32/{c}.m4a', 'fdk': LAD + '/ref/fdk-48k/he/32k/{c}.m4a',
           'apple': LAD + '/ref/apple-48k/he/32k/{c}.m4a'}
REG = [(0, 2000), (2000, 4000), (4000, 6000), (6000, 8000), (8000, 12000)]
RN = ['0-2', '2-4', '4-6', '6-8', '8-12']
CLIPS = sorted(os.path.basename(p) for p in glob.glob(AUDIO + '/*.wav'))

def parse(path):
    """-> {(frame,ch): dict(ws,gain,tns,glens,bands=[(g,s,e,cb,sf)])}, rate, fl"""
    ics = {}; bands = collections.defaultdict(list); rate = fl = None
    for ln in open(path):
        t = ln.split()
        if not t: continue
        if t[0] == 'R': rate, fl = int(t[1]), int(t[2])
        elif t[0] == 'I':
            ics[(int(t[1]), int(t[2]))] = dict(ws=int(t[3]), msfb=int(t[5]), gl=[int(x) for x in t[7].split(',') if x], gain=int(t[8]), tns=int(t[9]))
        elif t[0] == 'B':
            bands[(int(t[1]), int(t[2]))].append((int(t[3]), int(t[5]), int(t[6]), int(t[7]), int(t[8])))
    return ics, bands, rate, fl

def feat(ic, bl, rate, fl):
    """per-region line-weighted shares of pns/zero/is/spec/uncoded + meansf + cutoff"""
    short = ic['ws'] == 2; nl = fl // 8 if short else fl
    gl = ic['gl'] if short else [1]
    nyq = rate / 2.0
    acc = [dict(pns=0., zero=0., is_=0., spec=0.) for _ in REG]; sfs = []; sf45 = []; cut = 0
    for g, s, e, cb, sf in bl:
        w = (e - s) * (gl[g] if g < len(gl) else 1)
        fc = (s + e) / 2.0 * nyq / nl
        cut = max(cut, e * nyq / nl)
        if 1 <= cb <= 11: sfs.append(sf)
        for i, (lo, hi) in enumerate(REG):
            if lo <= fc < hi:
                k = 'pns' if cb == 13 else 'zero' if cb == 0 else 'is_' if cb in (14, 15) else 'spec'
                acc[i][k] += w
                if i >= 2 and 1 <= cb <= 11: sf45.append(sf)
    out = {}
    for i, (lo, hi) in enumerate(REG):
        tot = max(0., (min(hi, nyq) - lo)) / nyq * nl * (8 if short else 1)
        d = acc[i]
        out[RN[i]] = dict(tot=tot, **d, unc=max(0., tot - sum(d.values())))
    return dict(ws=ic['ws'], ng=len(ic['gl']) if short else 1, gain=ic['gain'], tns=ic['tns'], reg=out, msf=(float(np.mean(sfs)) if sfs else None),
                msf412=(float(np.mean(sf45)) if sf45 else None), cut=cut)

def lag_of(stream, c, tmp):
    import onsets as O, soundfile as sf
    src = getattr(extract, 'src', {})
    if c not in src:
        x, sr = sf.read(AUDIO + '/' + c, dtype='float64', always_2d=True); src = {c: O.mono48(x, sr)}; extract.src = src
    subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-i', stream, '-c', 'copy', '-f', 'adts', tmp + '/o.aac'], check=True)
    subprocess.run([FAAD, '-q', '-o', tmp + '/o.wav', tmp + '/o.aac'], check=True)
    x, sr = sf.read(tmp + '/o.wav', dtype='float64', always_2d=True)
    return O.est_lag(src[c], O.mono48(x, sr)), int(sr)

def pkt_bits(stream):
    r = subprocess.run(['ffprobe', '-v', 'error', '-select_streams', 'a:0', '-show_entries', 'packet=size', '-of', 'csv=p=0', stream], capture_output=True, text=True)
    return [int(x.strip(',')) * 8 for x in r.stdout.split()]

def extract():
    outp = W + '/frames.pkl'
    D = pickle.load(open(outp, 'rb')) if os.path.exists(outp) else {}
    for enc, pat in STREAMS.items():
        for c in CLIPS:
            if (enc, c) in D: continue
            st = pat.format(c=c)
            with tempfile.TemporaryDirectory(dir=W) as tmp:
                subprocess.run(['sh', HERE + '/decdump.sh', st, tmp + '/d.txt'], check=True)
                ics, bands, rate, fl = parse(tmp + '/d.txt')
                lag, osr = lag_of(st, c, tmp)
            F = {k: feat(ics[k], bands.get(k, []), rate, fl) for k in ics}
            D[(enc, c)] = dict(F=F, lag=lag, out_rate=osr, rate=rate, bits=pkt_bits(st))
        pickle.dump(D, open(outp, 'wb')); print(enc, 'done', flush=True)


def regsum(f, names):
    t = sum(f['reg'][n]['tot'] for n in names)
    d = {k: sum(f['reg'][n][k] for n in names) for k in ('pns', 'zero', 'is_', 'spec', 'unc')}
    den = max(t, sum(d.values()))
    return {k: (v / den if den else 0.) for k, v in d.items()}, den

def agg(rows, names):
    """mean over frames of line-weighted shares in region group (skip frames whose region is empty)"""
    acc = collections.defaultdict(list)
    for f in rows:
        s, den = regsum(f, names)
        if den <= 0: continue
        for k, v in s.items(): acc[k].append(v)
    return {k: (float(np.mean(v)) if v else None) for k, v in acc.items()}, len(acc['pns'])

def classes(rows):
    n = len(rows) or 1
    return {'short': sum(f['ws'] == 2 for f in rows) / n, 'start': sum(f['ws'] == 1 for f in rows) / n,
            'stop': sum(f['ws'] == 3 for f in rows) / n, 'long': sum(f['ws'] == 0 for f in rows) / n,
            'tns': sum(bool(f['tns']) for f in rows) / n}

def events(D):
    """per (enc, clip) list of dict(ch, s, k, frames)"""
    ons = json.load(open('/home/user/work/dec_streams/ref_onsets.json'))
    E = []
    for enc in STREAMS:
        for c in CLIPS:
            d = D[(enc, c)]
            for i, (ch, s) in enumerate(ons[c]['events']):
                k = int((s + d['lag']) * d['out_rate'] / 48000.0 // 2048)
                E.append(dict(enc=enc, clip=c, i=i, ch=ch, s=s, k=k))
    return E

def report():
    D = pickle.load(open(W + '/frames.pkl', 'rb')); E = events(D)
    rows = {}
    for l in open('/home/user/work/nb/rows.jsonl'):
        r = json.loads(l)
        if r['enc'] == 'faac' and r['rung'] == 32: rows[r['clip']] = r
    G = lambda e, j: D[(e['enc'], e['clip'])]['F'].get((e['k'] + j, e['ch']))
    B = lambda e, j: (lambda b: b[e['k'] + j] if 0 <= e['k'] + j < len(b) else None)(D[(e['enc'], e['clip'])]['bits'])
    R = {'meta': {'events': len(E) // 3}}
    GR = {'all 0-12k': RN, '0-2': ['0-2'], '2-4': ['2-4'], '4-6': ['4-6'], '6-8': ['6-8'], '8-12': ['8-12'], '4-12': ['4-6', '6-8', '8-12'], '2-12': RN[1:]}
    T = {}
    for enc in STREAMS:
        ev = [e for e in E if e['enc'] == enc]
        allf = [f for c in CLIPS for k_, f in D[(enc, c)]['F'].items()]
        allbits = [b for c in CLIPS for b in D[(enc, c)]['bits']]
        t = {'rate': D[(enc, CLIPS[0])]['rate']}
        for label, j in (('k-1', -1), ('k', 0)):
            fr = [G(e, j) for e in ev]; fr = [f for f in fr if f]
            bt = [B(e, j) for e in ev]; bt = [b for b in bt if b is not None]
            x = {'n': len(fr), 'classes': classes(fr), 'gain': float(np.mean([f['gain'] for f in fr])),
                 'msf': float(np.mean([f['msf'] for f in fr if f['msf'] is not None])),
                 'msf4-12': float(np.mean([f['msf412'] for f in fr if f['msf412'] is not None])),
                 'cut_hz': float(np.mean([f['cut'] for f in fr])), 'bits_frame': float(np.mean(bt)), 'regions': {}}
            for g, nm in GR.items():
                a, n = agg(fr, nm); x['regions'][g] = a
            t['onset ' + label] = x
        # all frames: split by short / long
        for lab, sel in (('all', lambda f: True), ('all long-type', lambda f: f['ws'] != 2), ('all short', lambda f: f['ws'] == 2)):
            fr = [f for f in allf if sel(f)]
            x = {'n': len(fr), 'classes': classes(fr), 'gain': float(np.mean([f['gain'] for f in fr])),
                 'msf': float(np.mean([f['msf'] for f in fr if f['msf'] is not None])),
                 'msf4-12': float(np.mean([f['msf412'] for f in fr if f['msf412'] is not None])),
                 'cut_hz': float(np.mean([f['cut'] for f in fr])), 'bits_frame': float(np.mean(allbits)), 'regions': {}}
            for g, nm in GR.items(): x['regions'][g] = agg(fr, nm)[0]
            t[lab] = x
        # onset frame k-1 split by its window type
        for lab, sel in (('onset k-1 short', lambda f: f['ws'] == 2), ('onset k-1 long-type', lambda f: f['ws'] != 2)):
            fr = [f for f in (G(e, -1) for e in ev) if f and sel(f)]
            if not fr: continue
            x = {'n': len(fr), 'classes': classes(fr), 'gain': float(np.mean([f['gain'] for f in fr])), 'regions': {}}
            for g, nm in GR.items(): x['regions'][g] = agg(fr, nm)[0]
            t[lab] = x
        T[enc] = t
    R['tables'] = T
    # ---- FAAC join
    J = []
    ev = [e for e in E if e['enc'] == 'faac']
    for e in ev:
        r = rows[e['clip']]; f1, f0 = G(e, -1), G(e, 0)
        a, b, fu = r['2-6k'][e['i']], r['6-12k'][e['i']], r['full'][e['i']]
        if f1 is None or a is None or b is None or fu is None: continue
        p = lambda x: max(0., x)
        s412, _ = regsum(f1, ['4-6', '6-8', '8-12']); s212, _ = regsum(f1, RN[1:]); s012, _ = regsum(f1, RN)
        s412b, _ = regsum(f0, ['4-6', '6-8', '8-12']) if f0 else ({'pns': np.nan}, 0)
        J.append(dict(clip=e['clip'], ch=e['ch'], s=e['s'], k=e['k'], ex26=p(a), ex612=p(b), exfull=p(fu), pns412=s412['pns'], zero412=s412['zero'] + s412['unc'],
                      spec412=s412['spec'], pns212=s212['pns'], pns012=s012['pns'], short=int(f1['ws'] == 2), bits=B(e, -1), bits2=(B(e, -1) or 0) + (B(e, 0) or 0),
                      ng=f1['ng'], gain=f1['gain'], tns=int(bool(f1['tns'])), pns412_k=s412b['pns'], msf=f1['msf']))
    R['join_n'] = len(J)
    import csv
    keys = list(J[0].keys())
    with open(W + '/faac_join.csv', 'w') as fh:
        w_ = csv.writer(fh); w_.writerow(keys); [w_.writerow([j[k] for k in keys]) for j in J]
    col = lambda k, rows=J: np.array([r[k] if r[k] is not None else np.nan for r in rows], float)
    clipv = np.array([j['clip'] for j in J])
    def demean(x):
        y = x.copy()
        for c in set(clipv):
            m = (clipv == c) & ~np.isnan(x); y[m] = x[m] - x[m].mean()
        return y
    cor = {}
    for tgt in ('ex26', 'ex612', 'exfull'):
        for f_ in ('pns412', 'pns212', 'pns012', 'zero412', 'spec412', 'short', 'bits', 'bits2', 'ng', 'gain', 'tns', 'pns412_k'):
            x, y = col(f_), col(tgt); m = ~np.isnan(x) & ~np.isnan(y)
            ok = len(set(x[m])) > 1
            rho, p = spearmanr(x[m], y[m]) if ok else (np.nan, np.nan)
            xd, yd = demean(x), demean(y)
            rw, pw = spearmanr(xd[m], yd[m]) if ok else (np.nan, np.nan)
            cor[f'{tgt}~{f_}'] = dict(rho=float(rho), p=float(p), rho_within_clip=float(rw), p_within=float(pw), n=int(m.sum()))
    R['spearman'] = cor
    splits = {}
    for f_ in ('pns412', 'zero412', 'bits'):
        x = col(f_); med = float(np.nanmedian(x)); hi = x > med; lo = x <= med
        sp = {'median': med, 'n_hi': int(hi.sum()), 'n_lo': int(lo.sum())}
        for tgt in ('ex26', 'ex612', 'exfull'):
            y = col(tgt)
            d = dict(hi=float(y[hi].mean()), lo=float(y[lo].mean())); d['diff'] = d['hi'] - d['lo']
            diffs = []
            for c in set(clipv):
                m = clipv == c
                if (hi & m).any() and (lo & m).any(): diffs.append(y[hi & m].mean() - y[lo & m].mean())
            d['within_clip_diff'] = float(np.mean(diffs)); d['within_clip_n'] = len(diffs)
            sp[tgt] = d
        splits[f_] = sp
    R['splits'] = splits
    p412 = col('pns412'); bins = {}
    edges = [-0.01, 0.0, 0.25, 0.5, 0.75, 0.9, 1.01]
    for lo_, hi_ in zip(edges[:-1], edges[1:]):
        m = (p412 > lo_) & (p412 <= hi_)
        if m.any(): bins[f'({max(lo_,0):.2f},{hi_ if hi_<=1 else 1:.2f}]' if lo_ >= 0 else '=0'] = dict(n=int(m.sum()), ex26=float(col('ex26')[m].mean()), ex612=float(col('ex612')[m].mean()), exfull=float(col('exfull')[m].mean()), bits=float(np.nanmean(col('bits')[m])))
    R['pns_bins'] = bins
    sh = col('short')
    R['short_split'] = {str(int(v)): dict(n=int((sh == v).sum()), ex26=float(col('ex26')[sh == v].mean()), ex612=float(col('ex612')[sh == v].mean()), exfull=float(col('exfull')[sh == v].mean())) for v in (0., 1.)}
    bd = {}; med = float(np.nanmedian(p412))
    for tgt in ('ex26', 'ex612', 'exfull'):
        y = col(tgt); tot = y.sum(); out = {}
        for thr in (0.5, 0.75):
            h = p412 > thr
            out[f'pns412>{thr}'] = dict(share_events=float(h.mean()), share_excess=float(y[h].sum() / tot), mean_hi=float(y[h].mean()), mean_rest=float(y[~h].mean()))
        hi = p412 > med; lo = float(y[~hi].mean())
        out['bound_pooled_reduction_db'] = float(np.clip(y[hi] - lo, 0, None).sum() / len(y))
        out['pooled'] = float(y.mean()); bd[tgt] = out
    R['bound'] = bd

    # group count per encoder at k-1 (short frames only)
    gs = {}
    for enc in STREAMS:
        fr = [f for f in (G(e, -1) for e in E if e['enc'] == enc) if f and f['ws'] == 2]
        gs[enc] = dict(n=len(fr), mean_groups=float(np.mean([f['ng'] for f in fr])) if fr else None,
                       dist={str(g): sum(f['ng'] == g for f in fr) / len(fr) for g in range(1, 9)} if fr else {})
    R['groups_k-1_short'] = gs
    ngv = col('ng'); R['ng_bins'] = {}
    for lo_, hi_ in ((1, 1), (2, 2), (3, 3), (4, 8)):
        m = (ngv >= lo_) & (ngv <= hi_)
        if m.any(): R['ng_bins'][f'{lo_}-{hi_}'] = dict(n=int(m.sum()), ex26=float(col('ex26')[m].mean()), ex612=float(col('ex612')[m].mean()), exfull=float(col('exfull')[m].mean()), pns412=float(p412[m].mean()), bits=float(np.nanmean(col('bits')[m])))
    # bits tercile x pns hi/lo (median split)
    bt = col('bits'); q = np.nanpercentile(bt, [33.3, 66.7]); strat = {}
    for nm, m in (('bits low', bt <= q[0]), ('bits mid', (bt > q[0]) & (bt <= q[1])), ('bits high', bt > q[1])):
        for pn, pm in (('pns>0.9', p412 > 0.9), ('pns<=0.9', p412 <= 0.9)):
            mm = m & pm
            if mm.sum(): strat[f'{nm} / {pn}'] = dict(n=int(mm.sum()), ex26=float(col('ex26')[mm].mean()), ex612=float(col('ex612')[mm].mean()), exfull=float(col('exfull')[mm].mean()))
    R['bits_x_pns'] = strat; R['bits_terciles'] = [float(x) for x in q]
    R['spearman_pns412_bits'] = float(spearmanr(p412[~np.isnan(bt)], bt[~np.isnan(bt)])[0])
    # within-clip bound: events with pns412 above median replaced by their clip's below-median mean (if lower)
    wb = {}
    for tgt in ('ex26', 'ex612', 'exfull'):
        y = col(tgt); red = 0.; hi = p412 > med
        for c in set(clipv):
            m = clipv == c
            if (hi & m).any() and (~hi & m).any():
                lo_m = y[~hi & m].mean(); red += np.clip(y[hi & m] - lo_m, 0, None).sum()
        wb[tgt] = dict(pooled=float(y.mean()), within_clip_bound_reduction=float(red / len(y)))
    R['within_clip_bound'] = wb
    # pns-share-matched comparison with refs: FAAC onset 4-12k PNS vs all-short

    # paired comparison with FDK/Apple on the same events, split by FAAC onset-frame PNS saturation
    refrows = {}
    for l in open('/home/user/work/nb/rows.jsonl'):
        r = json.loads(l)
        if r['rung'] == 32: refrows[(r['enc'], r['clip'])] = r
    pair = {}
    keyl = [(j['clip'], j['i']) if 'i' in j else None for j in J]
    for enc in ('faac', 'fdk', 'apple'):
        pair[enc] = {}
        for tgt, key in (('ex26', '2-6k'), ('ex612', '6-12k'), ('exfull', 'full')):
            v = []
            for e in ev:
                f1 = G(e, -1); r = refrows[(enc, e['clip'])]
                x = r[key][e['i']]
                v.append(np.nan if (f1 is None or x is None) else max(0., x))
            pair[enc][tgt] = np.array(v)
    sat = np.array([regsum(G(e, -1), ['4-6', '6-8', '8-12'])[0]['pns'] > 0.9 for e in ev])
    R['paired_by_faac_pns_sat'] = {}
    for tgt in ('ex26', 'ex612', 'exfull'):
        d = {'share_events_sat': float(sat.mean())}
        for enc in pair:
            y = pair[enc][tgt]
            d[enc] = dict(all=float(np.nanmean(y)), sat=float(np.nanmean(y[sat])), nonsat=float(np.nanmean(y[~sat])))
        for enc in ('fdk', 'apple'):
            d['gap_faac_minus_' + enc] = dict(total=d['faac']['all'] - d[enc]['all'],
                                              from_sat=float(np.nansum((pair['faac'][tgt] - pair[enc][tgt])[sat]) / len(sat)),
                                              from_nonsat=float(np.nansum((pair['faac'][tgt] - pair[enc][tgt])[~sat]) / len(sat)))
        R['paired_by_faac_pns_sat'][tgt] = d
    # characteristics of saturated frames
    f1s = [G(e, -1) for e in ev]
    R['sat_frame_profile'] = {lab: {rn: float(np.mean([f['reg'][rn]['pns'] / max(1e-9, max(f['reg'][rn]['tot'], sum(f['reg'][rn][k] for k in ('pns','zero','is_','spec','unc')))) for f, m in zip(f1s, mk) if m])) for rn in RN}
                             for lab, mk in (('sat', sat), ('nonsat', ~sat))}
    R['sat_clips'] = {}
    for c in sorted(set(clipv)):
        m = clipv == c; R['sat_clips'][c] = [int(m.sum()), float(sat[m].mean())]
    # share of clips: events concentration
    cs = np.array([v[1] for v in R['sat_clips'].values()])
    R['sat_clip_dist'] = dict(clips_all_sat=int((cs > 0.99).sum()), clips_none=int((cs < 0.01).sum()), clips_mixed=int(((cs >= 0.01) & (cs <= 0.99)).sum()))

    # clip-balanced view and leave-fatboy-out (one clip holds 44 % of the events)
    clips_u = sorted(set(clipv)); FB_ = '13 fatboy_30sec.wav'
    cb = {}
    for tgt in ('ex26', 'ex612', 'exfull'):
        cb[tgt] = {}
        for enc in pair:
            y = pair[enc][tgt]
            pc_ = {c: float(np.nanmean(y[clipv == c])) for c in clips_u}
            cb[tgt][enc] = dict(clip_mean=float(np.mean(list(pc_.values()))),
                                clip_mean_excl_fatboy=float(np.mean([v for c, v in pc_.items() if c != FB_])),
                                fatboy=pc_[FB_], pooled_excl_fatboy=float(np.nanmean(y[clipv != FB_])))
    R['clip_balanced'] = cb
    R['fatboy'] = dict(n=int((clipv == FB_).sum()), share_events=float((clipv == FB_).mean()), share_sat=float(sat[clipv == FB_].mean()),
                       share_of_sat_events=float((sat & (clipv == FB_)).sum() / sat.sum()))
    nf = clipv != FB_; sx = {}
    for tgt in ('ex26', 'ex612', 'exfull'):
        y = col(tgt)
        for f_ in ('pns412', 'pns012', 'bits', 'ng'):
            x = col(f_); m = nf & ~np.isnan(x) & ~np.isnan(y)
            rho, p = spearmanr(x[m], y[m]); xd, yd = demean(x), demean(y)
            rw, pw = spearmanr(xd[m], yd[m])
            sx[f'{tgt}~{f_}'] = dict(rho=float(rho), p=float(p), rho_within_clip=float(rw), p_within=float(pw), n=int(m.sum()))
    R['spearman_excl_fatboy'] = sx
    sp2 = {}
    m412 = p412[nf]; med2 = float(np.median(m412))
    for tgt in ('ex26', 'ex612', 'exfull'):
        y = col(tgt)[nf]; hi = m412 > med2
        sp2[tgt] = dict(median=med2, n_hi=int(hi.sum()), n_lo=int((~hi).sum()), hi=float(y[hi].mean()), lo=float(y[~hi].mean()))
    R['split_excl_fatboy'] = sp2
    # within fatboy: sat vs nonsat
    fm = clipv == FB_
    R['fatboy_sat_split'] = {tgt: dict(sat=float(col(tgt)[fm & sat].mean()), nonsat=(float(col(tgt)[fm & ~sat].mean()) if (fm & ~sat).any() else None), n_nonsat=int((fm & ~sat).sum())) for tgt in ('ex26', 'ex612', 'exfull')}
    # clip-level: share of FAAC onset events (per clip) PNS-saturated vs clip mean excess
    cs_ = np.array([sat[clipv == c].mean() for c in clips_u]); cm_ = np.array([np.nanmean(col('ex26')[clipv == c]) for c in clips_u])
    R['clip_level_spearman_sat_vs_ex26'] = float(spearmanr(cs_, cm_)[0]); R['clips_with_events'] = len(clips_u)
    # per-clip PNS share at onset vs decoded-frame all
    json.dump(R, open(OUT + '/he32_core_onsets.json', 'w'), indent=1, default=float)
    return R

if __name__ == '__main__':
    if sys.argv[1] == 'extract': extract()
    elif sys.argv[1] == 'report': report()
    elif sys.argv[1] == 'md': pass


def md():
    R = json.load(open(OUT + '/he32_core_onsets.json')); T = R['tables']; L = []
    P = lambda x: '-' if x is None else f'{100*x:.0f}%'
    L += ['### A. Window sequence, TNS, gain, bits at the onset frame (per-channel events, 965; k-1 = frame holding the onset)', '',
          '| stream | frame set | n | short | start | stop | long | TNS | mean global gain | mean sf | frame bits | coded cutoff Hz |', '|---|---|---|---|---|---|---|---|---|---|---|---|']
    for enc in ('faac', 'fdk', 'apple'):
        for lab in ('onset k-1', 'onset k', 'all', 'all long-type', 'all short'):
            x = T[enc][lab]; c = x['classes']
            L.append(f"| {enc} | {lab} | {x['n']} | {P(c['short'])} | {P(c['start'])} | {P(c['stop'])} | {P(c['long'])} | {P(c['tns'])} | {x['gain']:.1f} | {x['msf']:.1f} | {x['bits_frame']:.0f} | {x['cut_hz']:.0f} |")
    L += ['', 'Frame bits = access-unit size of the container packet (all elements, incl. SBR payload). Apple 32k is a 32 kHz-output stream (64 ms frames), so its bits are not per-time comparable.', '',
          '### B. Band-type shares by region (line-weighted per ICS, mean over frames; PNS = codebook 13, IS = 14/15, zero = codebook 0 within max_sfb, unc = above max_sfb / beyond the coded cutoff; spectral = the rest)', '',
          '| stream | frame set | region | PNS | zero | IS | spectral | uncoded |', '|---|---|---|---|---|---|---|---|']
    for enc in ('faac', 'fdk', 'apple'):
        for lab in ('onset k-1', 'all long-type', 'all short'):
            for rg in ('0-2', '2-4', '4-6', '6-8', '8-12'):
                a = T[enc][lab]['regions'][rg]
                if not a: continue
                L.append(f"| {enc} | {lab} | {rg} | {P(a['pns'])} | {P(a['zero'])} | {P(a['is_'])} | {P(a['spec'])} | {P(a['unc'])} |")
    L += ['', 'FAAC onset frames also: `onset k-1` is 100 % EIGHT_SHORT, so short-vs-long cannot be tested inside FAAC. FDK/Apple cores end at 5.6-6.5 kHz (their 6-8 and 8-12 kHz regions are uncoded = SBR).', '']
    L += ['### C. FAAC 32k: per-event pre-echo (nper_bands cache) vs onset-frame (k-1) decisions', '',
          '| target | feature | Spearman (events) | n | within-clip | excl. fatboy | excl. fatboy within-clip |', '|---|---|---|---|---|---|---|']
    S, X = R['spearman'], R['spearman_excl_fatboy']
    for tgt in ('ex26', 'ex612', 'exfull'):
        for f_ in ('pns412', 'pns012', 'zero412', 'spec412', 'bits', 'ng', 'gain', 'short', 'tns'):
            s_ = S[f'{tgt}~{f_}']; x_ = X.get(f'{tgt}~{f_}')
            fm = lambda v: 'n/a (constant)' if v != v else f'{v:+.2f}'
            L.append(f"| {tgt} | {f_} | {fm(s_['rho'])} | {s_['n']} | {fm(s_['rho_within_clip'])} | {fm(x_['rho']) if x_ else ''} | {fm(x_['rho_within_clip']) if x_ else ''} |")
    L += ['', 'ex26 / ex612 / exfull = per-event max(0, dB) pre-echo in 2-6 kHz / 6-12 kHz / benchmark full band. pns412 = PNS line share in 4-12 kHz (the 2-12 kHz and 0-12 kHz variants are pns212/pns012), zero412 = zero + uncoded share in 4-12 kHz, ng = number of window groups, bits = packet bits of frame k-1.', '',
          '### D. Split tables', '', '| split | n hi / lo | ex26 hi / lo | ex612 hi / lo | exfull hi / lo | within-clip diff ex26 / ex612 / exfull (clips) |', '|---|---|---|---|---|---|']
    for f_, sp in R['splits'].items():
        if f_ == 'zero412': continue
        L.append(f"| {f_} > median {sp['median']:.2f} | {sp['n_hi']} / {sp['n_lo']} | {sp['ex26']['hi']:.1f} / {sp['ex26']['lo']:.1f} | {sp['ex612']['hi']:.1f} / {sp['ex612']['lo']:.1f} | {sp['exfull']['hi']:.1f} / {sp['exfull']['lo']:.1f} | {sp['ex26']['within_clip_diff']:+.1f} / {sp['ex612']['within_clip_diff']:+.1f} / {sp['exfull']['within_clip_diff']:+.1f} ({sp['ex26']['within_clip_n']}) |")
    sp = R['split_excl_fatboy']
    L.append(f"| pns412 > median {sp['ex26']['median']:.2f}, excl. fatboy | {sp['ex26']['n_hi']} / {sp['ex26']['n_lo']} | {sp['ex26']['hi']:.1f} / {sp['ex26']['lo']:.1f} | {sp['ex612']['hi']:.1f} / {sp['ex612']['lo']:.1f} | {sp['exfull']['hi']:.1f} / {sp['exfull']['lo']:.1f} | |")
    L += ['', 'Zero-band share is not testable: 963 of 965 FAAC onset frames have <= 8.7 % zero/uncoded lines in 4-12 kHz.', '',
          '| PNS share 4-12 kHz (k-1) | n | ex26 | ex612 | exfull | frame bits |', '|---|---|---|---|---|---|']
    for k, b in R['pns_bins'].items(): L.append(f"| {k} | {b['n']} | {b['ex26']:.1f} | {b['ex612']:.1f} | {b['exfull']:.1f} | {b['bits']:.0f} |")
    L += ['', 'Window groups at k-1 (short frames): ' + '; '.join(f"{e} mean {v['mean_groups']:.2f}" for e, v in R['groups_k-1_short'].items()) + '.', '',
          '### E. One clip dominates the pooled event numbers', '',
          f"13 fatboy_30sec holds {R['fatboy']['n']} of {R['join_n']} events ({100*R['fatboy']['share_events']:.0f} %), {100*R['fatboy']['share_sat']:.0f} % of them have PNS > 90 % of the 4-12 kHz lines, and it holds {100*R['fatboy']['share_of_sat_events']:.0f} % of all such events. Only {R['clips_with_events']} of 77 clips have scored events.", '',
          '| target | enc | pooled events | clip-mean (36 clips) | clip-mean excl. fatboy | fatboy | pooled excl. fatboy |', '|---|---|---|---|---|---|---|']
    pp = R['paired_by_faac_pns_sat']
    for tgt in ('ex26', 'ex612', 'exfull'):
        for enc in ('faac', 'fdk', 'apple'):
            c = R['clip_balanced'][tgt][enc]
            L.append(f"| {tgt} | {enc} | {pp[tgt][enc]['all']:.2f} | {c['clip_mean']:.2f} | {c['clip_mean_excl_fatboy']:.2f} | {c['fatboy']:.1f} | {c['pooled_excl_fatboy']:.2f} |")
    L += ['', '### F. Paired on the same events, split by FAAC onset-frame PNS saturation (pns412 > 0.9; 418 events, 43 %)', '',
          '| target | enc | all | sat | non-sat |', '|---|---|---|---|---|']
    for tgt in ('ex26', 'ex612', 'exfull'):
        for enc in ('faac', 'fdk', 'apple'):
            d = pp[tgt][enc]; L.append(f"| {tgt} | {enc} | {d['all']:.1f} | {d['sat']:.1f} | {d['nonsat']:.1f} |")
    L += ['', '| target | FAAC-FDK pooled gap (dB) | from sat events | from non-sat | FAAC-Apple gap | from sat | from non-sat |', '|---|---|---|---|---|---|---|']
    for tgt in ('ex26', 'ex612', 'exfull'):
        a, b = pp[tgt]['gap_faac_minus_fdk'], pp[tgt]['gap_faac_minus_apple']
        L.append(f"| {tgt} | {a['total']:.2f} | {a['from_sat']:.2f} | {a['from_nonsat']:.2f} | {b['total']:.2f} | {b['from_sat']:.2f} | {b['from_nonsat']:.2f} |")
    L += ['', 'Bits x PNS stratification (bit terciles of k-1 packet size: %s): ' % ', '.join(f'{x:.0f}' for x in R['bits_terciles']) + '; '.join(f"{k}: n={v['n']}, ex26 {v['ex26']:.1f}, ex612 {v['ex612']:.1f}" for k, v in R['bits_x_pns'].items()) + f". Spearman(pns412, bits) = {R['spearman_pns412_bits']:.2f}.", '']
    return '\n'.join(L)

if __name__ == '__main__' and sys.argv[1] == 'md':
    print(md())
