#!/usr/bin/env python3
"""HE core CPE band-class shares per region x window + per-clip IS/MS vs stereo fidelity.
usage: he_stereo_analysis.py CACHEDIR OUTDIR"""
import sys, json, glob, os
from collections import defaultdict
import numpy as np
from scipy.stats import spearmanr
CD, OD = sys.argv[1], sys.argv[2]
RN = ["0-1", "1-2", "2-4", "4-6", "6-8", "8-12"]; CATS = ["ms", "is", "pns", "zero", "lr"]
ENCS = ["faac", "apple", "fdk", "xaac"]; RUNGS = [32, 40, 48]
ARM = {"faac": "faac", "apple": "apple", "fdk": "fdkref", "xaac": "xaacref"}
ST = defaultdict(dict)
base = "/home/user/faac/probe/ladder/results/s12-apple48k/stage1-rebaseline/"
for f, filt in ((base + "rows_he_cbr.jsonl", lambda x: x["arm"] == "faac" and x["mode"] == "cbr"),
                (base + "rows_he_ref.jsonl", lambda x: x["arm"] != "faac")):
    for l in open(f):
        x = json.loads(l)
        if x["decoder"] == "faad2" and filt(x) and x.get("stfid") is not None:
            ST[(x["arm"], x["rung"])][x["clip"]] = x["stfid"]
agg = {}; perclip = {}
for e in ENCS:
    for r in RUNGS:
        a = defaultdict(int)
        for f in glob.glob("%s/%s%d|*.json" % (CD, e, r)):
            c = os.path.basename(f)[len("%s%d|" % (e, r)):-5]
            d = json.load(open(f)); tot = isn = ms = 0
            for k, v in d["stereo"].items():
                a[k] += v; tot += v; isn += v if k.endswith("|is") else 0; ms += v if k.endswith("|ms") else 0
            perclip[(e, r, c)] = dict(n=tot, is_share=isn / tot if tot else None, ms_share=ms / tot if tot else None)
        agg[(e, r)] = a
def share_table(a, wt):
    rows = []
    for i, rn in enumerate(RN):
        cnt = {c: a.get("%s|%d|%s" % (wt, i, c), 0) for c in CATS}; n = sum(cnt.values())
        rows.append(dict(region=rn, n=n, **{c: (cnt[c] / n if n else None) for c in CATS}))
    return rows
J = {"shares": {}, "per_clip": {}, "spearman": {}}
md = ["# HE core CPE stereo band shares (FAAC master CBR vs Apple vs FDK vs xaac)", "",
      "Source: instrumented libfaad dump of the HE-AAC v1 core (24 kHz core, CPE band pairs below max_sfb), 77 clips per stream. "
      "Class priority per band pair: IS (either channel cb 14/15) > PNS (cb 13) > zero (both cb 0) > M/S (mask) > L/R. Region by band centre (Hz at the 24 kHz core). Shares are % of band pairs (band count, not lines).", ""]
for r in RUNGS:
    for wt in ("long", "short"):
        md += ["## %d kbps, %s windows" % (r, wt), "", "| region (kHz) | encoder | n bands | M/S % | IS % | PNS % | zero % | L/R % |", "|---|---|---|---|---|---|---|---|"]
        for i, rn in enumerate(RN):
            for e in ENCS:
                row = share_table(agg[(e, r)], wt)[i]
                J["shares"].setdefault("%s%d" % (e, r), {}).setdefault(wt, []).append(row)
                p = lambda v: "-" if v is None else "%.1f" % (100 * v)
                md.append("| %s | %s | %d | %s | %s | %s | %s | %s |" % (rn, e, row["n"], *[p(row[c]) for c in CATS]))
        md.append("")
# window mix
md += ["## Share of CPE band pairs in short windows", "", "| rung | " + " | ".join(ENCS) + " |", "|---|" + "---|" * 4]
for r in RUNGS:
    cells = []
    for e in ENCS:
        a = agg[(e, r)]; s = sum(v for k, v in a.items() if k.startswith("short")); t = sum(a.values())
        cells.append("%.1f" % (100 * s / t)); J.setdefault("short_band_share", {})["%s%d" % (e, r)] = s / t
    md.append("| %d | %s |" % (r, " | ".join(cells)))
# per-clip
md += ["", "## Per-clip means (mean over clips of per-clip share, %)", "", "| rung | encoder | mean M/S % | mean IS % | median IS % | clips with IS>1% |", "|---|---|---|---|---|---|"]
for r in RUNGS:
    for e in ENCS:
        v = [perclip[(e, r, c)] for (ee, rr, c) in perclip if ee == e and rr == r and perclip[(e, r, c)]["n"]]
        isv = np.array([x["is_share"] for x in v]); msv = np.array([x["ms_share"] for x in v])
        md.append("| %d | %s | %.1f | %.1f | %.1f | %d |" % (r, e, 100 * msv.mean(), 100 * isv.mean(), 100 * np.median(isv), (isv > .01).sum()))
        J["per_clip"]["%s%d" % (e, r)] = {c: perclip[(e, r, c)] for (ee, rr, c) in perclip if ee == e and rr == r}
# stfid mean + Spearman
md += ["", "## Stereo fidelity (faad2 `stfid`) and per-clip join", "", "Spearman rho (p) over clips of  dFID = stfid_FAAC - stfid_X  vs  dIS = IS_FAAC - IS_X and dMS = MS_FAAC - MS_X (per-clip band share).", "",
       "| rung | X | n clips | mean stfid FAAC | mean stfid X | rho(dFID,dIS) | p | rho(dFID,dMS) | p | rho(dFID,dIS+dMS) | p |", "|---|---|---|---|---|---|---|---|---|---|---|"]
for r in RUNGS:
    for x in ("fdk", "apple", "xaac"):
        cl = sorted(set(ST[("faac", r)]) & set(ST[(ARM[x], r)]) & {c for (e, rr, c) in perclip if e == "faac" and rr == r} & {c for (e, rr, c) in perclip if e == x and rr == r})
        dF = np.array([ST[("faac", r)][c] - ST[(ARM[x], r)][c] for c in cl])
        dI = np.array([perclip[("faac", r, c)]["is_share"] - perclip[(x, r, c)]["is_share"] for c in cl])
        dM = np.array([perclip[("faac", r, c)]["ms_share"] - perclip[(x, r, c)]["ms_share"] for c in cl])
        s1 = spearmanr(dF, dI); s2 = spearmanr(dF, dM); s3 = spearmanr(dF, dI + dM)
        mf = np.mean([ST[("faac", r)][c] for c in cl]); mx = np.mean([ST[(ARM[x], r)][c] for c in cl])
        J["spearman"]["%d_faac_minus_%s" % (r, x)] = dict(n=len(cl), mean_stfid_faac=mf, mean_stfid_x=mx,
              rho_is=s1.statistic, p_is=s1.pvalue, rho_ms=s2.statistic, p_ms=s2.pvalue, rho_ismsum=s3.statistic, p_ismsum=s3.pvalue,
              mean_dIS=float(dI.mean()), mean_dMS=float(dM.mean()), mean_dFID=float(dF.mean()))
        md.append("| %d | %s | %d | %.4f | %.4f | %.2f | %.3g | %.2f | %.3g | %.2f | %.3g |" % (r, x, len(cl), mf, mx, s1.statistic, s1.pvalue, s2.statistic, s2.pvalue, s3.statistic, s3.pvalue))
J["per_clip_stfid"] = {"%s%d" % (e, r): ST[(ARM[e], r)] for e in ENCS for r in RUNGS}
md += ["", "(mean dIS / dMS / dFID, FAAC minus X, in the JSON under `spearman`.)"]
open(OD + "/he_stereo_shares.md", "w").write("\n".join(md) + "\n")
json.dump(J, open(OD + "/he_stereo_shares.json", "w"), indent=1)
print("\n".join(md[-40:]))
