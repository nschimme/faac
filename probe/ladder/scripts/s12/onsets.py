#!/usr/bin/env python3
"""Onset-aligned window/TNS comparison (S12 task B).

Onsets: the reference events the benchmark's transient.py actually scores (ref_onsets.onsets,
which wraps transient._scored_pre_echo_events: candidates -> refined -> eligible), per channel,
merged across channels when within MERGE samples.
Alignment: each stream is remuxed (ffmpeg -c copy) to ADTS so faad does not trim the
encoder delay, decoded with FAAD2, and the lag of decoded vs source is estimated by
cross-correlation of the mono sums (first 10 s, at 48 kHz).  The decoded stream's frame k
(= raw_data_block index of the decdump) covers output samples [k*L, (k+1)*L), L = 1024 (LC)
or 2048 (HE, at the declared output rate).  frame(onset) = floor((onset_out + lag_out) / L).
usage: onsets.py [--lags-cache F] [--seqs dec_seqs.pkl] [--stats dec_stats_raw.json] OUTDIR
"""
import os, sys, json, glob, pickle, subprocess, tempfile, argparse
from math import gcd
from collections import defaultdict, Counter
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import numpy as np, soundfile as sf, scipy.signal as ss

AUDIO = os.environ["AUDIO"]; FAAD = os.environ["FAAD"]
REF = "/home/user/faac/probe/ladder/ref"; DS = "/home/user/work/dec_streams"
MERGE = 64
STREAMS = {  # key -> (profile, label, path_fn)
}
def _p(enc, prof, rung):
    if enc == "faac":
        d = {"lc": "abr%d" % rung, "he": "he%d" % rung}[prof]
        return lambda c: "%s/%s/%s.m4a" % (DS, d, c)
    return lambda c: "%s/%s-48k/%s/%dk/%s.m4a" % (REF, enc, prof, rung, c)
for enc, prof, rungs in (("faac", "lc", (96, 128, 192)), ("apple", "lc", (128, 192)), ("fdk", "lc", (128, 192)),
                         ("faac", "he", (32, 48)), ("apple", "he", (32, 48)), ("fdk", "he", (32, 48))):
    for r in rungs:
        STREAMS["%s-%s%d" % (enc, prof, r)] = (prof, enc, r, _p(enc, prof, r))
SEQKEY = {k: (k.replace("-lc", "-lc").replace("faac-lc", "faac-abr")) for k in STREAMS}


def mono48(x, sr):
    m = x.sum(1)
    if sr != 48000:
        g = gcd(sr, 48000); m = ss.resample_poly(m, 48000 // g, sr // g)
    return m


def est_lag(src, dec, n=480000):
    a = src[:n]; b = dec[:n + 16384]
    c = ss.fftconvolve(b, a[::-1], mode="full")
    k = int(np.argmax(np.abs(c)))
    return k - (len(a) - 1)


def get_onsets(cache):
    if os.path.exists(cache):
        return json.load(open(cache))
    import ref_onsets
    out = {}
    for w in sorted(glob.glob(AUDIO + "/*.wav")):
        ev, cov, sr, n = ref_onsets.onsets(w)
        out[os.path.basename(w)] = {"events": ev, "coverage": cov, "sr": sr, "n": n}
    json.dump(out, open(cache, "w"))
    return out


def merged(events):
    """per-channel events -> list of (sample, {chs})"""
    ev = sorted((s, c) for c, s in events)
    out = []
    for s, c in ev:
        if out and s - out[-1][0] <= MERGE:
            out[-1][1].add(c)
        else:
            out.append([s, {c}])
    return [(s, sorted(cs)) for s, cs in out]


def get_lags(lagfile, clips):
    lags = json.load(open(lagfile)) if os.path.exists(lagfile) else {}
    tmp = tempfile.mkdtemp(dir="/home/user/work")
    srccache = {}
    n = 0
    for key, (prof, enc, r, fn) in STREAMS.items():
        for c in clips:
            kk = key + "|" + c
            if kk in lags: continue
            if c not in srccache:
                x, sr = sf.read(AUDIO + "/" + c, dtype="float64", always_2d=True)
                srccache = {c: mono48(x, sr)}
            subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", fn(c), "-c", "copy", "-f", "adts", tmp + "/o.aac"], check=True)
            subprocess.run([FAAD, "-q", "-o", tmp + "/o.wav", tmp + "/o.aac"], check=True)
            x, sr = sf.read(tmp + "/o.wav", dtype="float64", always_2d=True)
            lags[kk] = {"lag48": est_lag(srccache[c], mono48(x, sr)), "out_rate": int(sr), "len": len(x)}
            n += 1
            if n % 50 == 0:
                json.dump(lags, open(lagfile, "w")); print("lags", n, flush=True)
        json.dump(lags, open(lagfile, "w")); print(key, "lags done", flush=True)
    return lags


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("outdir")
    ap.add_argument("--seqs", default="/home/user/work/dec_seqs.pkl")
    ap.add_argument("--stats", default="/home/user/work/dec_stats_raw.json")
    ap.add_argument("--onsets-cache", default="/home/user/work/dec_streams/ref_onsets.json")
    ap.add_argument("--lags-cache", default="/home/user/work/onset_lags.json")
    a = ap.parse_args()
    ons = get_onsets(a.onsets_cache)
    clips = sorted(ons)
    lags = get_lags(a.lags_cache, clips)
    seqs = pickle.load(open(a.seqs, "rb")); stats = json.load(open(a.stats))
    OFFS = (-2, -1, 0, 1, 2)
    jl = open(a.outdir + "/onsets_per_onset.jsonl", "w")
    res = {}  # key -> accum
    for key, (prof, enc, r, fn) in STREAMS.items():
        sk = key.replace("faac-lc", "faac-abr")
        L = 1024 if prof == "lc" else 2048
        R = dict(n=0, cls=Counter(), off_short={o: 0 for o in OFFS}, near_short=0, pre=0, short_k=0, clips={}, nofr=0)
        for c in clips:
            q = seqs[sk][c]["seq"]
            W = {}; T = {}; NF = {}
            for fr, ch, ws, tns, nf in q:
                W.setdefault(fr, {})[ch] = ws; T.setdefault(fr, {})[ch] = tns
            lg = lags[key + "|" + c]; orate = lg["out_rate"]
            cc = R["clips"].setdefault(c, dict(n=0, cls=Counter()))
            for s, chs in merged(ons[c]["events"]):
                k = int(np.floor((s + lg["lag48"]) * orate / 48000.0 / L))
                def ws_at(f):
                    d = W.get(f)
                    if not d: return None
                    sel = [d[x] for x in chs if x in d] or list(d.values())
                    return 2 if 2 in sel else sel[0]
                def tns_at(f):
                    d = T.get(f)
                    if not d: return None
                    sel = [d[x] for x in chs if x in d] or list(d.values())
                    return int(any(sel))
                w = [ws_at(k + j) for j in (-1, 0, 1)]; t = [tns_at(k + j) for j in (-1, 0, 1)]
                if w[1] is None: cl = "other"
                elif w[1] == 2: cl = "short"
                elif t[1]: cl = "long+tns"
                else: cl = "long"
                R["n"] += 1; R["cls"][cl] += 1; cc["n"] += 1; cc["cls"][cl] += 1
                for o in OFFS:
                    if ws_at(k + o) == 2: R["off_short"][o] += 1
                if any(x == 2 for x in w): R["near_short"] += 1
                if w[1] == 1 and w[2] == 2: R["pre"] += 1
                jl.write(json.dumps({"clip": c, "onset": s, "chs": chs, "stream": key, "enc": enc, "profile": prof, "rung": r,
                                     "frame": k, "class": cl, "ws": w, "tns": t}) + "\n")
        res[key] = R
    jl.close()
    # TNS summary from raw decstats (all ICS, whole streams)
    tns = {}
    for key, (prof, enc, r, fn) in STREAMS.items():
        sk = key.replace("faac-lc", "faac-abr")
        t = stats[sk]["aggregate"]["tns"]; row = {}
        for wt in ("short", "long"):
            x = t.get(wt, {"ics": 0, "tns_share": None, "nfilt_dist": {}, "order_dist": {}})
            on = sum(x["nfilt_dist"].values())
            nfilt = sum(int(k) * v for k, v in x["nfilt_dist"].items())
            od = x["order_dist"]; no = sum(od.values())
            row[wt] = dict(ics=x["ics"], tns_on=on, share=(on / x["ics"] if x["ics"] else None),
                           mean_filters=(nfilt / on if on else None),
                           mean_order=(sum(int(k) * v for k, v in od.items()) / no if no else None))
        tns[key] = row
    for prof in ("lc", "he"):
        write(prof, res, tns, ons, clips, a.outdir)


def pc(x, n): return "-" if not n else "%.1f" % (100.0 * x / n)


def label(key):
    prof, enc, r, _ = STREAMS[key]
    return "%s %s %dk" % ({"faac": "FAAC", "apple": "Apple", "fdk": "FDK"}[enc], prof.upper(), r)


def write(prof, res, tns, ons, clips, outdir):
    keys = [k for k in STREAMS if STREAMS[k][0] == prof]
    CL = ("short", "long+tns", "long", "other")
    j = {"streams": {}, "tns": {k: tns[k] for k in keys}, "per_clip": {}}
    md = ["# Onset-aligned window/TNS comparison (%s)" % prof.upper(), "",
          "Onsets = reference events scored by the benchmark's `transient.py` (eligible events, per channel, channels merged when within %d samples); %d clips, %d onsets (%d per-channel events)." % (
              MERGE, len(clips), res[keys[0]]["n"], sum(len(ons[c]["events"]) for c in clips)),
          "Alignment: per stream/clip lag by cross-correlation of decoded (ADTS remux, FAAD2, untrimmed) vs source mono sum, first 10 s; frame k = floor((onset+lag)/L), L = %d output samples." % (1024 if prof == "lc" else 2048),
          "Classes at frame k: `short` = EIGHT_SHORT at k; `long+tns` = long-type (ONLY_LONG/START/STOP) with TNS on (either channel of the onset's channels); `long` = long-type, no TNS; `other` = frame missing.",
          "(LONG_START at k-1 + EIGHT_SHORT at k is a subset of `short`.)", "",
          "## Class shares (% of onsets)", "", "| stream | onsets | short | long+tns | long | other |", "|---|---|---|---|---|---|"]
    for k in keys:
        R = res[k]; n = R["n"]
        md.append("| %s | %d | %s | %s | %s | %s |" % (label(k), n, *[pc(R["cls"][c], n) for c in CL]))
        j["streams"][k] = {"label": label(k), "n": n, "class_counts": dict(R["cls"]),
                           "class_share": {c: R["cls"][c] / n for c in CL}}
    md += ["", "## Alignment validation: share of onsets with EIGHT_SHORT at frame k+offset (%)", "",
           "| stream | k-2 | k-1 | k | k+1 | k+2 | any of k-1..k+1 | START at k & SHORT at k+1 |", "|---|---|---|---|---|---|---|---|"]
    for k in keys:
        R = res[k]; n = R["n"]
        md.append("| %s | %s | %s | %s | %s | %s | %s | %s |" % (label(k), *[pc(R["off_short"][o], n) for o in (-2, -1, 0, 1, 2)], pc(R["near_short"], n), pc(R["pre"], n)))
        j["streams"][k]["short_by_offset"] = {str(o): R["off_short"][o] / n for o in R["off_short"]}
        j["streams"][k]["short_any_pm1"] = R["near_short"] / n
        j["streams"][k]["start_k_short_k1"] = R["pre"] / n
    # per clip
    md += ["", "## Per-clip class shares (% short / % long+tns) for each stream", "",
           "| clip | n | " + " | ".join(label(k) for k in keys) + " |", "|---|---|" + "---|" * len(keys)]
    for c in clips:
        n = res[keys[0]]["clips"][c]["n"]
        if not n: continue
        cells = []
        for k in keys:
            cc = res[k]["clips"][c]["cls"]
            cells.append("%s / %s" % (pc(cc["short"], n), pc(cc["long+tns"], n)))
        md.append("| %s | %d | %s |" % (c, n, " | ".join(cells)))
        j["per_clip"][c] = {"n": n, **{k: {x: res[k]["clips"][c]["cls"][x] / n for x in CL} for k in keys}}
    md += ["", "## TNS usage (all ICS of each stream, 77 clips)", "",
           "| stream | short ICS | short w/ TNS % | mean filters | mean order | long ICS | long w/ TNS % | mean filters | mean order |", "|---|---|---|---|---|---|---|---|---|"]
    f = lambda x: "-" if x is None else "%.2f" % x
    for k in keys:
        s, l = tns[k]["short"], tns[k]["long"]
        md.append("| %s | %d | %s | %s | %s | %d | %s | %s | %s |" % (label(k), s["ics"], "-" if s["share"] is None else "%.2f" % (100 * s["share"]), f(s["mean_filters"]), f(s["mean_order"]),
                                                                  l["ics"], "-" if l["share"] is None else "%.2f" % (100 * l["share"]), f(l["mean_filters"]), f(l["mean_order"])))
    open("%s/onsets_%s.md" % (outdir, prof), "w").write("\n".join(md) + "\n")
    json.dump(j, open("%s/onsets_%s.json" % (outdir, prof), "w"), indent=1)


if __name__ == "__main__":
    main()
