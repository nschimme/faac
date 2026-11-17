#!/usr/bin/env python3
"""Grid-vs-onset metrics from sbrgrid_run.py caches. Same conventions as
sbr_grid_analysis.py: onsets merged within 64 samples over channels, frame k =
floor((onset + lag + delta) / 2048), position = remainder / 128 slots, a border is
interior when not 0/16, near-N = any onset channel's interior border within N slots.
usage as module: metrics(dir, tag, clips, delta=-256)"""
import json, os
import numpy as np
ONS = json.load(open("/home/user/work/dec_streams/ref_onsets.json"))
MERGE, SLOT, NS, L = 64, 128, 16, 2048
CLS = ["FIXFIX", "FIXVAR", "VARFIX", "VARVAR"]

def merged(events):
    ev = sorted((s, c) for c, s in events); out = []
    for s, c in ev:
        if out and s - out[-1][0] <= MERGE: out[-1][1].add(c)
        else: out.append([s, {c}])
    return [(s, sorted(cs)) for s, cs in out]

def metrics(d, tag, clips, delta=-256, lagfix=None, mono_grid=False):
    n = nogrid = near2 = near1 = ffo = 0
    nfr = nvar = 0; cls = {}; trans = {}; dist = []; off = {}
    for clip in clips:
        f = "%s/%s|%s.json" % (d, tag, clip)
        r = json.load(open(f))
        g = {(a[0], a[1]): a for a in r["grid"]}
        # frame-level class counts and transitions per channel
        last = {}
        for a in r["grid"]:
            nfr += 1; nvar += a[2] != 0; cls[CLS[a[2]]] = cls.get(CLS[a[2]], 0) + 1
            p = last.get(a[1])
            if p is not None:
                k = CLS[p] + ">" + CLS[a[2]]; trans[k] = trans.get(k, 0) + 1
            last[a[1]] = a[2]
        lag = r["lag48"] if lagfix is None else lagfix
        for s, chs in merged(ONS[clip]["events"]):
            p = (s + lag + delta) * r["out_rate"] // 48000
            k = p // L; pos = (p - k * L) / float(SLOT)
            # HE-v2: one SBR channel (the mono carrier) serves both onset channels
            gl = [g[(k, 0 if mono_grid else ch)] for ch in chs if (k, 0 if mono_grid else ch) in g]
            if not gl: nogrid += 1; continue
            n += 1
            ffo += gl[0][2] == 0
            bi = [t for a in gl for t in a[4] if t not in (0, NS)]
            if bi:
                dd = min(abs(pos - t) for t in bi); dist.append(dd)
                near2 += dd <= 2; near1 += dd <= 1
                t = min(bi, key=lambda t: abs(pos - t)); o = int(round(pos - t)); off[o] = off.get(o, 0) + 1
    return dict(n=n, nogrid=nogrid, near2=near2 / n if n else None, near1=near1 / n if n else None,
                fixfix_at_onset=ffo / n if n else None, nonfixfix=nvar / nfr if nfr else None,
                frames=nfr, classes=cls, transitions=trans, offsets=dict(sorted(off.items())))

if __name__ == "__main__":
    import sys
    d, tag, lst = sys.argv[1:4]
    clips = [l.strip() for l in open(lst) if l.strip()]
    print(json.dumps(metrics(d, tag, clips), indent=1))
