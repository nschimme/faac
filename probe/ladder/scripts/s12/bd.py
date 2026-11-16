#!/usr/bin/env python3
"""BD-rate + per-rung tables between two arms from harness rows (faac-benchmark scripts/bd_rate.bd_rate_curve).
Positive BD-rate = test needs more bits than ref (worse)."""
import argparse, json, os, sys
import numpy as np

FB = os.environ.get('FB', '/home/user/work/fb')
sys.path.insert(0, FB)
from scripts.bd_rate import bd_rate_curve  # noqa: E402


REFARMS = {'apple', 'fdkref', 'xaacref'}


def load(files, mode, profile, decoder, metric='mos'):
    d = {}  # (arm, clip, rung) -> (kbps, metric)  (last row wins)
    for f in files:
        for l in open(f):
            r = json.loads(l)
            if r.get('decoder') != decoder or r.get('error') or r.get(metric) is None:
                continue
            # packaged refs have one stream set; match them in every mode
            if mode and r['mode'] != mode and r['arm'] not in REFARMS or profile and r['profile'] != profile:
                continue
            d[(r['arm'], r['clip'], int(r['rung']))] = (r['kbps_realized'], r[metric])
    return d


def boot_ci(x, n=10000, seed=0):
    x = np.asarray(x, float)
    if len(x) == 0:
        return None, None
    rng = np.random.default_rng(seed)
    m = x[rng.integers(0, len(x), size=(n, len(x)))].mean(axis=1)
    return float(np.percentile(m, 2.5)), float(np.percentile(m, 97.5))


def analyze(files, test, ref, rungs, decoder, mode=None, profile=None, exclude=(), metric='mos', top=15):
    d = load(files, mode, profile, decoder, metric)
    clips = sorted({k[1] for k in d if k[0] in (test, ref)} - set(exclude))
    per_clip, invalid = {}, []
    for c in clips:
        bp = [d.get((ref, c, r)) for r in rungs]
        cp = [d.get((test, c, r)) for r in rungs]
        if any(p is None for p in bp + cp):
            invalid.append((c, 'missing rung')); continue
        bd = bd_rate_curve(bp, cp)
        if bd is None:
            invalid.append((c, 'no valid curve')); continue
        per_clip[c] = bd
    v = np.array(list(per_clip.values()))
    lo, hi = boot_ci(v)
    out = dict(test=test, ref=ref, rungs=rungs, decoder=decoder, mode=mode, profile=profile, metric=metric,
               n_clips=len(clips), n_valid=len(v), n_no_curve=len(invalid), no_curve=invalid,
               bd_rate_mean=float(v.mean()) if len(v) else None, bd_ci95=[lo, hi],
               bd_rate_median=float(np.median(v)) if len(v) else None, per_clip_bd=per_clip, rung_table=[], wins={}, worst=[])
    for r in rungs:
        t = [d[(test, c, r)] for c in clips if (test, c, r) in d and (ref, c, r) in d]
        b = [d[(ref, c, r)] for c in clips if (test, c, r) in d and (ref, c, r) in d]
        if not t:
            continue
        tm, bm = [x[1] for x in t], [x[1] for x in b]
        dm = np.array(tm) - np.array(bm)
        out['rung_table'].append(dict(rung=r, n=len(t), test_mos=float(np.mean(tm)), ref_mos=float(np.mean(bm)),
                                      test_kbps=float(np.mean([x[0] for x in t])), ref_kbps=float(np.mean([x[0] for x in b])),
                                      test_p1=float(np.percentile(tm, 1)), ref_p1=float(np.percentile(bm, 1)),
                                      delta_mos=float(dm.mean()), wins=int((dm > 0).sum()), ties=int((dm == 0).sum()),
                                      losses=int((dm < 0).sum())))
    rows = []
    for r in rungs:
        for c in clips:
            if (test, c, r) in d and (ref, c, r) in d:
                rows.append((d[(test, c, r)][1] - d[(ref, c, r)][1], c, r))
    rows.sort()
    out['worst'] = [dict(delta=x, clip=c, rung=r) for x, c, r in rows[:top]]
    return out


def md(o):
    L = [f"# {o['test']} vs {o['ref']}  (decoder {o['decoder']}, {o['mode']}/{o['profile']}, rungs {o['rungs']}, metric {o['metric']})", '']
    ci = o['bd_ci95']
    L.append(f"BD-rate mean **{o['bd_rate_mean']:+.3f}%** (95% clip-bootstrap CI [{ci[0]:+.3f}, {ci[1]:+.3f}], 10000 resamples, seed 0), "
             f"median {o['bd_rate_median']:+.3f}%; valid curves {o['n_valid']}/{o['n_clips']}, no valid curve: {o['n_no_curve']}" if o['n_valid'] else
             f"no valid BD curves ({o['n_no_curve']}/{o['n_clips']} clips)")
    L += ['', '| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |', '|---|---|---|---|---|---|---|---|---|---|']
    for t in o['rung_table']:
        L.append(f"| {t['rung']} | {t['n']} | {t['test_mos']:.4f} | {t['ref_mos']:.4f} | {t['delta_mos']:+.4f} | {t['test_kbps']:.2f} | "
                 f"{t['ref_kbps']:.2f} | {t['test_p1']:.3f} | {t['ref_p1']:.3f} | {t['wins']}/{t['ties']}/{t['losses']} |")
    L += ['', 'Worst clip x rung dMOS (test - ref at equal nominal rung):', '', '| dMOS | rung | clip |', '|---|---|---|']
    L += [f"| {w['delta']:+.4f} | {w['rung']} | {w['clip']} |" for w in o['worst']]
    if o['no_curve']:
        L += ['', 'No valid curve: ' + ', '.join(f'{c} ({why})' for c, why in o['no_curve'])]
    return '\n'.join(L) + '\n'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rows', nargs='+')
    ap.add_argument('--test', required=True)
    ap.add_argument('--ref', required=True)
    ap.add_argument('--rungs', required=True)
    ap.add_argument('--decoder', default='faad2')
    ap.add_argument('--mode'); ap.add_argument('--profile')
    ap.add_argument('--metric', default='mos')
    ap.add_argument('--exclude-clips', help='file with clip names')
    ap.add_argument('--out', help='prefix for .md/.json')
    a = ap.parse_args()
    ex = [l.strip() for l in open(a.exclude_clips) if l.strip()] if a.exclude_clips else []
    o = analyze(a.rows, a.test, a.ref, [int(x) for x in a.rungs.split(',')], a.decoder, a.mode, a.profile, ex, a.metric)
    s = md(o)
    print(s)
    if a.out:
        open(a.out + '.md', 'w').write(s); json.dump(o, open(a.out + '.json', 'w'), indent=1)


if __name__ == '__main__':
    main()
