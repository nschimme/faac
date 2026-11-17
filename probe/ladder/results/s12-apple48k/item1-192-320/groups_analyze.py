#!/usr/bin/env python3
import json, csv, os, numpy as np
from scipy.stats import spearmanr
D = os.path.dirname(os.path.abspath(__file__)) + '/'
R = '/home/user/faac/probe/ladder/results/s12-apple48k/stage1-rebaseline/'
C = json.load(open(D + 'cache.json'))
bd = json.load(open(R + 'lc_abr/faac_vs_apple_faad2_192-256-320_all.json'))['per_clip_lin']
worse = [l.strip() for l in open(R + 'lc_abr/g192_worse.txt') if l.strip()]
better = [l.strip() for l in open(R + 'lc_abr/g192_better.txt') if l.strip()]
clips = [c for c in worse + better if all(f'{a}|{c}|{r}' in C['rows'] for a in ('faac','apple') for r in (192,256,320))]
G = {c: ('worse' if c in worse else 'better') for c in clips}
FE = ['bw_hz','pns_share_6k','is_share','ms_share','zero_0-6','zero_6-12','zero_12-16','zero_16-20','sf_0-6','sf_6-12','sf_12-16','sf_16-20','short_share','tns_share','kbps']
SD = ['e12k','e16k','onsets_per_s','lr_corr','rms_db']
rows = []
for c in clips:
    for r in (192,256,320):
        f, a = C['rows'][f'faac|{c}|{r}'], C['rows'][f'apple|{c}|{r}']
        row = {'clip': c, 'group': G[c], 'rung': r, 'bd_lin': bd[c]}
        for k in FE:
            row['faac_'+k] = f[k]; row['apple_'+k] = a[k]; row['d_'+k] = f[k]-a[k]
        for k in SD: row['src_'+k] = C['src'][c][k]
        rows.append(row)
with open(D + 'groups_perclip.csv', 'w', newline='') as fh:
    w = csv.DictWriter(fh, fieldnames=list(rows[0])); w.writeheader(); w.writerows(rows)
def arr(rs, k): return np.array([r[k] for r in rs], float)
out = {'n_clips': len(clips), 'n_worse': len(worse), 'groups': {}, 'spearman': {}}
cols = [p+k for k in FE for p in ('faac_','apple_','d_')] + ['src_'+k for k in SD]
for rung in ('all',192,256,320):
    rs = [r for r in rows if rung == 'all' or r['rung'] == rung]
    gg = {}
    for col in cols:
        e = {}
        for g in ('worse','better'):
            v = arr([r for r in rs if r['group']==g], col)
            e[g] = {'mean': float(np.nanmean(v)), 'median': float(np.nanmedian(v))}
        gg[col] = e
    out['groups'][str(rung)] = gg
    sp = []
    for col in [c for c in cols if c.startswith('d_') or c.startswith('src_')]:
        if rung == 'all' and col.startswith('d_') or rung != 'all' or col.startswith('src_'):
            pass
        # per-clip: for 'all' average over rungs per clip
        if rung == 'all':
            xs = [np.nanmean(arr([r for r in rs if r['clip']==c], col)) for c in clips]
        else:
            xs = [r[col] for r in rs]
        ys = [bd[c] for c in clips]
        m = ~np.isnan(xs)
        rho, p = spearmanr(np.array(xs)[m], np.array(ys)[m])
        sp.append({'feature': col, 'rho': float(rho), 'p': float(p), 'n': int(m.sum())})
    sp.sort(key=lambda d: -abs(d['rho']) if d['rho']==d['rho'] else 0)
    out['spearman'][str(rung)] = sp
json.dump(out, open(D + 'groups.json', 'w'), indent=1)
L = [f"# Item 1: worse vs better clips (LC ABR 192/256/320, FAAC master vs Apple)\n",
     f"Clips: {len(clips)} ({sum(1 for c in clips if G[c]=='worse')} worse, {sum(1 for c in clips if G[c]=='better')} better). Sources: groups_extract.py -> cache.json; groups_analyze.py; per-clip groups_perclip.csv.",
     "Features from libfaad decision dump (long windows unless stated). Regions by band centre at 48 kHz. Zero share is by spectral line (bands beyond max_sfb count as zero). PNS share = PNS bands / non-zero bands >6 kHz. IS share = IS bands / non-zero bands. M/S share = ms_used among non-zero ch0 bands. Bandwidth = per-frame max over channels of highest cb 1-11 band edge, p95 over long frames. Scalefactor mean over cb 1-11 bands. Source: energy share via Welch PSD, onsets/s = benchmark eligible pre-echo events per channel-second, lr_corr = L/R Pearson, rms_db dBFS. No causal claim; rho is Spearman vs per-clip linear BD (positive = FAAC needs more bits). Spearman uses per-clip values averaged across rungs ('all') or per rung.\n"]
for rung in ('all',192,256,320):
    gg = out['groups'][str(rung)]
    L.append(f"## Group means / medians ({'all rungs pooled' if rung=='all' else str(rung)+' kbps'})\n")
    L.append("| feature | W FAAC | B FAAC | W Apple | B Apple | W diff | B diff | diff mean W-B |\n|---|---|---|---|---|---|---|---|")
    fm = lambda e: f"{e['mean']:.3g} / {e['median']:.3g}"
    for k in FE:
        L.append(f"| {k} | {fm(gg['faac_'+k]['worse'])} | {fm(gg['faac_'+k]['better'])} | {fm(gg['apple_'+k]['worse'])} | {fm(gg['apple_'+k]['better'])} | {fm(gg['d_'+k]['worse'])} | {fm(gg['d_'+k]['better'])} | {gg['d_'+k]['worse']['mean']-gg['d_'+k]['better']['mean']:.3g} |")
    for k in SD:
        e = gg['src_'+k]
        L.append(f"| src_{k} | W {fm(e['worse'])} | B {fm(e['better'])} | | | | | {e['worse']['mean']-e['better']['mean']:.3g} |")
    L.append("\n(cells: mean / median; W = worse group, B = better group)\n")
    L.append(f"### Spearman vs per-clip linear BD, ranked by |rho| ({'clip-mean over rungs' if rung=='all' else str(rung)+' kbps'})\n")
    L.append("| rank | feature | rho | p | n |\n|---|---|---|---|---|")
    for i, s in enumerate(out['spearman'][str(rung)], 1):
        L.append(f"| {i} | {s['feature']} | {s['rho']:+.3f} | {s['p']:.3g} | {s['n']} |")
    L.append("")
open(D + 'groups.md', 'w').write("\n".join(L))
