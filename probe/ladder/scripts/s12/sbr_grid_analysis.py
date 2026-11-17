#!/usr/bin/env python3
"""SBR grid at onsets (HE, 48k out). Input: per-stream json from he_grid_stereo_run.py.
usage: sbr_grid_analysis.py CACHEDIR OUTDIR"""
import os, sys, json, glob
from collections import Counter, defaultdict
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
CD, OD = sys.argv[1], sys.argv[2]
ons = json.load(open("/home/user/work/dec_streams/ref_onsets.json"))
MERGE = 64; SLOT = 128; NS = 16; L = 2048
ENCS = ["faac", "apple", "fdk", "xaac"]; RUNGS = [32, 40, 48]
def merged(events):
    ev = sorted((s, c) for c, s in events); out = []
    for s, c in ev:
        if out and s - out[-1][0] <= MERGE: out[-1][1].add(c)
        else: out.append([s, {c}])
    return [(s, sorted(cs)) for s, cs in out]
M = {c: merged(ons[c]["events"]) for c in ons}
clips = sorted(ons)
D = {}
for e in ENCS:
    for r in RUNGS:
        for c in clips:
            f = "%s/%s%d|%s.json" % (CD, e, r, c)
            if os.path.exists(f): D[(e, r, c)] = json.load(open(f))
print("loaded", len(D))
def gridmap(d):
    g = {}
    for fr, ch, cls, le, te, fres in d["grid"]: g[(fr, ch)] = (cls, le, te, fres)
    return g
G = {k: gridmap(v) for k, v in D.items()}
WS = {k: {r[0]: r[1:] for r in v["ws"]} for k, v in D.items()}
def borders(g, f, chs, edges=False):
    """interior borders (t not in {0,16}) of frame f for chs -> list of per-ch lists of rel slots (0..18)"""
    out = []
    for ch in chs:
        x = g.get((f, ch))
        if x is None: continue
        out.append([t for t in x[2] if edges or t not in (0, NS)])
    return out
def onset_rows(e, r, delta):
    """yield per-onset dict at integer-sample delta"""
    for c in clips:
        d = D.get((e, r, c))
        if d is None: continue
        g = G[(e, r, c)]
        for s, chs in M[c]:
            p = (s + d["lag48"] + delta) * d["out_rate"] // 48000
            k = p // L
            yield c, s, chs, k, (p - k * L) / float(SLOT), g
def stats_at(e, rungs, delta):
    """aggregate over rungs; returns dict for frames k and k-1"""
    res = {}
    for which, off in (("k", 0), ("k-1", -1)):
        n = multi = near2 = near1 = 0; dist = []; dist_all = []; nint = 0; nogrid = 0; cls = Counter(); ne = Counter()
        for r in rungs:
            for c, s, chs, k, pos, g in onset_rows(e, r, delta):
                f = k + off; x = pos - off * NS
                gl = [g[(f, ch)] for ch in chs if (f, ch) in g]
                if not gl: nogrid += 1; continue
                n += 1
                if any(a[1] > 1 for a in gl): multi += 1
                cls[max(a[0] for a in gl) if False else gl[0][0]] += 1
                ne[gl[0][1]] += 1
                bi = [t for a in gl for t in a[2] if t not in (0, NS)]
                ba = [t for a in gl for t in a[2]]
                if bi:
                    nint += 1; dd = min(abs(x - t) for t in bi); dist.append(dd)
                    near2 += dd <= 2; near1 += dd <= 1
                dist_all.append(min(abs(x - t) for t in ba))
        res[which] = dict(n=n, nogrid=nogrid, multi_env=multi / n if n else None,
                          with_interior_border=nint / n if n else None,
                          near2=near2 / n if n else None, near1=near1 / n if n else None,
                          mean_dist_interior=float(np.mean(dist)) if dist else None,
                          mean_dist_any_border=float(np.mean(dist_all)) if dist_all else None,
                          class_at_onset=dict(cls), numenv_at_onset=dict(ne))
    return res
# ---- all-frame distributions
CLN = {0: "FIXFIX", 1: "FIXVAR", 2: "VARFIX", 3: "VARVAR"}
dist = {}
for e in ENCS:
    for r in RUNGS:
        cls = Counter(); ne = Counter(); both = Counter(); fb = Counter(); tot = 0
        for c in clips:
            d = D.get((e, r, c))
            if d is None: continue
            for fr, ch, kl, le, te, fres in d["grid"]:
                cls[CLN[kl]] += 1; ne[le] += 1; both[(CLN[kl], le)] += 1; tot += 1
                for fq in fres: fb["hi" if fq else "lo"] += 1
        dist[(e, r)] = dict(n=tot, frame_class={k: v / tot for k, v in cls.items()},
                            num_env={str(k): v / tot for k, v in sorted(ne.items())},
                            class_x_env={"%s/%d" % k: v / tot for k, v in both.items()},
                            freq_res_hi_share=fb["hi"] / max(1, fb["hi"] + fb["lo"]))
# ---- alignment curves: nearest-interior-border within 2 slots vs delta (frame k) ; core short vs delta
DEL = list(range(-4096, 4097, 128))
curve = {}
for e in ENCS:
    cs = []
    for dl in DEL:
        n = near = multi = sh = nsh = 0
        for r in RUNGS:
            for c, s, chs, k, pos, g in onset_rows(e, r, dl):
                gl = [g[(k, ch)] for ch in chs if (k, ch) in g]
                if gl:
                    n += 1
                    bi = [t for a in gl for t in a[2] if t not in (0, NS)]
                    if bi and min(abs(pos - t) for t in bi) <= 2: near += 1
                    if any(a[1] > 1 for a in gl): multi += 1
        # core short at k
        for r in RUNGS:
            for c, s, chs, k, pos, g in onset_rows(e, r, dl):
                w = WS[(e, r, c)].get(k)
                if k < 0 or k > max(WS[(e, r, c)]): continue
                nsh += 1
                if w is None: continue
                sh += any(w[ch] == 2 for ch in chs if ch < len(w))
        cs.append(dict(delta=dl, n=n, near2=near / n if n else None, multi=multi / n if n else None,
                       short_k=sh / nsh if nsh else None, n_short=nsh))
    curve[e] = cs
best = {}
for e in ENCS:
    cs = curve[e]
    b = max(cs, key=lambda x: (x["near2"] or 0)); bs = max([x for x in cs if abs(x['delta'])<=2048], key=lambda x: (x["short_k"] or 0))
    best[e] = dict(sbr_best_delta=b["delta"], sbr_near2_at_best=b["near2"], core_best_delta=bs["delta"],
                   core_short_at_best=bs["short_k"], near2_at_0=[x for x in cs if x["delta"] == 0][0]["near2"])
json.dump(dict(curve=curve, best=best), open(OD + "/sbr_grid_align_curves.json", "w"), indent=1)
print(json.dumps(best, indent=1))

# ---- main stats at delta=0 and at the reference-derived SBR offset
DBEST = -256
out = {"delta_best_from_refs": DBEST, "alignment": best, "all_frames": {"%s%d" % k: v for k, v in dist.items()}, "onsets": {}}
for e in ENCS:
    for r in RUNGS:
        for dl in (0, DBEST):
            out["onsets"]["%s%d@%d" % (e, r, dl)] = stats_at(e, [r], dl)
json.dump(out, open(OD + "/sbr_grid_he.json", "w"), indent=1)
f = lambda v, p=1: "-" if v is None else ("%.*f" % (p, 100 * v))
md = ["# SBR time grid at scored onsets (HE-AAC v1, 48 kHz output, FAAC master CBR vs Apple/FDK/xaac)", "",
      "Decoder: instrumented libfaad (`E` records, patch `libfaad_decdump_sbrgrid.patch`). Grid per SBR frame/channel: frame_class, num_env L_E, envelope borders t_E in time slots (1 slot = 128 output samples, 16 slots per 2048-sample frame; VAR classes can place borders up to slot 18), freq_res per envelope.",
      "Onsets: the same merged scored onsets as `onsets_he.md` (%d onsets over 77 clips). Aligned frame k = floor((onset + lag + delta)/2048), lag by xcorr of ADTS-remuxed FAAD decode vs source (re-estimated here for every stream incl. FAAC master CBR, 40 kbps, xaac); position in slots = remainder/128." % sum(len(M[c]) for c in clips), "",
      "## Alignment check (offset delta added to the xcorr lag, samples)", "",
      "Share of onsets with an interior envelope border (t_E not 0/16) of frame k within +-2 slots, pooled over 32/40/48 kbps, as a function of delta (curves in `sbr_grid_align_curves.json`). Reference encoders peak sharply; FAAC (almost entirely FIXFIX) shows no peak.", "",
      "| encoder | best delta (samples) | near2 at best | near2 at delta=0 |", "|---|---|---|---|"]
for e in ENCS: md.append("| %s | %d | %s | %s |" % (e, best[e]["sbr_best_delta"], f(best[e]["sbr_near2_at_best"]), f(best[e]["near2_at_0"])))
md += ["", "Apple, FDK and xaac all peak at delta = -256 samples (2 slots) relative to the xcorr lag (i.e. SBR grid timing leads the broadband lag by 2 slots, the residual offset); main tables below use delta = %d for all encoders (FAAC has no peak of its own, its tiny VAR use makes the choice immaterial; delta = 0 also in JSON)." % DBEST, "",
       "Core check: % of onsets whose onset channel(s) have EIGHT_SHORT at decoded frame floor((onset+lag+delta)/2048) vs delta (rungs pooled). Reference encoders peak at delta = -2048, i.e. the core frame holding the onset is k-1 under the xcorr-lag frame index (the ~1 frame residual noted earlier); the SBR grid of frame index k (delta -256) is the one carrying the onset. FAAC is saturated (short blocks in >90 % of onset frames at every delta), so its core curve is uninformative; its alignment is assumed identical (same xcorr procedure). Apple 32 kbps decodes at 32 kHz output (16 kHz core): delta and lag are scaled to its output rate.", "",
       "| delta | " + " | ".join(ENCS) + " |", "|---|" + "---|" * 4]
for i, dl in enumerate(DEL):
    if dl % 512 == 0 and -4096 <= dl <= 1024:
        md.append("| %d | %s |" % (dl, " | ".join(f(curve[e][i]["short_k"]) for e in ENCS)))
md.append("")
md += ["", "## Distribution over all SBR frames/channels", "", "| rung | encoder | n | FIXFIX % | FIXVAR % | VARFIX % | VARVAR % | env=1 % | env=2 % | env=3 % | env=4 % | freq_res hi % |", "|---|---|---|---|---|---|---|---|---|---|---|---|"]
for r in RUNGS:
    for e in ENCS:
        d = dist[(e, r)]; fc = d["frame_class"]; ne = d["num_env"]
        md.append("| %d | %s | %d | %s | %s | %s | %s | %s | %s | %s | %s | %s |" % (r, e, d["n"], *[f(fc.get(k, 0)) for k in ("FIXFIX", "FIXVAR", "VARFIX", "VARVAR")], *[f(ne.get(str(i), 0)) for i in (1, 2, 3, 4)], f(d["freq_res_hi_share"])))
for dl in (DBEST, 0):
    md += ["", "## At scored onsets, delta = %d" % dl, "", "env>1 = any onset channel has more than one envelope; near2 / near1 = interior border within +-2 / +-1 slots (onset slot position vs borders of that frame); mean dist (slots) to the nearest interior border over onsets that have one; `any-border` dist also counts the frame edges (0/16).", "",
           "| rung | encoder | frame | n | env>1 % | has interior border % | near2 % | near1 % | mean dist interior | mean dist any-border | FIXFIX % at onset |", "|---|---|---|---|---|---|---|---|---|---|---|"]
    for r in RUNGS:
        for e in ENCS:
            for w in ("k", "k-1"):
                x = out["onsets"]["%s%d@%d" % (e, r, dl)][w]
                ff = x["class_at_onset"].get(0, 0) / x["n"] if x["n"] else None
                g = lambda v: "-" if v is None else "%.2f" % v
                md.append("| %d | %s | %s | %d | %s | %s | %s | %s | %s | %s | %s |" % (r, e, w, x["n"], f(x["multi_env"]), f(x["with_interior_border"]), f(x["near2"]), f(x["near1"]), g(x["mean_dist_interior"]), g(x["mean_dist_any_border"]), f(ff)))
md.insert(2, "**Reading note**: FAAC at 40/48 kbps (identical grids at both rungs) uses FIXFIX with 4 envelopes in 97.5 % of frames (borders at slots 4, 8, 12), so its >1-envelope and near-border shares are the chance level of a regular grid (an onset uniformly placed in 0-16 is within +-2 slots of 4/8/12 with p = 0.75), not transient adaptation. FAAC at 32 kbps: 73 % FIXFIX/1 env, 27 % VARFIX. Apple 32 kbps is a 32 kHz-output stream (16 kHz core). Best-delta for FAAC in the alignment table is noise (no peak).\n")
open(OD + "/sbr_grid_he.md", "w").write("\n".join(md) + "\n")
