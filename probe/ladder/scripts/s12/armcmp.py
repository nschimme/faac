#!/usr/bin/env python3
"""Arm vs control per rung and decoder: worst MOS cells, pooled-NPER and stereo-fidelity differences with clip-bootstrap CIs."""
import json, sys, argparse, collections, numpy as np
ap = argparse.ArgumentParser(); ap.add_argument('rows', nargs='+'); ap.add_argument('--arm', required=True); ap.add_argument('--ctl', required=True)
ap.add_argument('--mode'); ap.add_argument('--profile'); ap.add_argument('--top', type=int, default=8); a = ap.parse_args()
D = collections.defaultdict(dict)
for f in a.rows:
    for l in open(f):
        r = json.loads(l)
        if r.get('error') or r['arm'] not in (a.arm, a.ctl): continue
        if a.mode and r['mode'] != a.mode or a.profile and r['profile'] != a.profile: continue
        D[(r['decoder'], int(r['rung']), r['arm'])][r['clip']] = r
rng = np.random.default_rng(0)
def pooled(R, cl): 
    ev = np.array([R[c].get('nper_events') or 0 for c in cl], float); v = np.array([(R[c].get('nper') or 0) for c in cl], float)
    return v, ev
print(f'# {a.arm} vs {a.ctl}\n')
print('| decoder | rung | n | dMOS mean | worst dMOS (clip) | cells < -0.10 | NPER ctl | NPER arm | dNPER [95% CI] | dStFid [95% CI] | dkbps % |')
print('|---|---|---|---|---|---|---|---|---|---|---|')
worst = []
for (dec, rung, arm) in sorted(D):
    if arm != a.arm: continue
    A, C = D[(dec, rung, a.arm)], D[(dec, rung, a.ctl)]
    cl = sorted(set(A) & set(C))
    dm = np.array([A[c]['mos'] - C[c]['mos'] for c in cl])
    i = int(np.argmin(dm)); worst += [(dm[j], dec, rung, cl[j]) for j in range(len(cl))]
    va, ea = pooled(A, cl); vc, ec = pooled(C, cl)
    def pd(idx): return (va[idx]*ea[idx]).sum()/max(ea[idx].sum(),1) - (vc[idx]*ec[idx]).sum()/max(ec[idx].sum(),1)
    bs = [pd(rng.integers(0, len(cl), len(cl))) for _ in range(4000)]
    sa = np.array([(A[c].get('stfid') or np.nan) - (C[c].get('stfid') or np.nan) for c in cl])
    sb = [np.nanmean(sa[rng.integers(0, len(cl), len(cl))]) for _ in range(4000)]
    dk = 100*(np.mean([A[c]['kbps_realized'] for c in cl])/np.mean([C[c]['kbps_realized'] for c in cl]) - 1)
    print(f'| {dec} | {rung} | {len(cl)} | {dm.mean():+.4f} | {dm[i]:+.3f} ({cl[i][:22]}) | {(dm < -0.10).sum()} | {(vc*ec).sum()/ec.sum():.2f} | {(va*ea).sum()/ea.sum():.2f} | {pd(np.arange(len(cl))):+.2f} [{np.percentile(bs,2.5):+.2f}, {np.percentile(bs,97.5):+.2f}] | {np.nanmean(sa):+.4f} [{np.percentile(sb,2.5):+.4f}, {np.percentile(sb,97.5):+.4f}] | {dk:+.2f} |')
worst.sort()
print('\nWorst clip x rung x decoder cells:\n\n| dMOS | decoder | rung | clip |\n|---|---|---|---|')
for w in worst[:a.top]: print(f'| {w[0]:+.4f} | {w[1]} | {w[2]} | {w[3]} |')
