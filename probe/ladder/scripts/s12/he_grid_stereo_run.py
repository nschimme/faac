#!/usr/bin/env python3
"""Per-stream pass for HE sbr-grid + core-stereo study: remux->ADTS, decode with FAAD_DECDUMP
(instrumented faad), lag by xcorr vs source, parse E (SBR grid) and B/I/F (core stereo) records.
Caches one json per (enc,rung,clip) in OUT. usage: he_grid_stereo_run.py OUT"""
import os, sys, json, glob, subprocess, tempfile
from math import gcd
from collections import defaultdict
import numpy as np, soundfile as sf, scipy.signal as ss
AUDIO = os.environ["AUDIO"]; BIN = "/home/user/work/faad-dump/build-rel/frontend/faad"
REF = "/home/user/faac/probe/ladder/ref"
def path(enc, r, c):
    if enc == "faac": return "/home/user/work/out/faac/he/cbr/%d/%s.m4a" % (r, c)
    d = {"apple": "apple-48k", "fdk": "fdk-48k", "xaac": "xaac-48k"}[enc]
    return "%s/%s/he/%dk/%s.m4a" % (REF, d, r, c)
REG = [(0, 1000), (1000, 2000), (2000, 4000), (4000, 6000), (6000, 8000), (8000, 12000)]
def region(f):
    for i, (lo, hi) in enumerate(REG):
        if lo <= f < hi: return i
    return len(REG) - 1
def mono48(x, sr):
    m = x.sum(1)
    if sr != 48000:
        g = gcd(sr, 48000); m = ss.resample_poly(m, 48000 // g, sr // g)
    return m
def est_lag(src, dec, n=480000):
    a = src[:n]; b = dec[:n + 16384]
    c = ss.fftconvolve(b, a[::-1], mode="full")
    return int(np.argmax(np.abs(c))) - (len(a) - 1)

def parse(dump):
    grid = []  # (frame, ch, cls, L_E, t_E, freq_res)
    wsq = {}; cur = {}; bands = defaultdict(dict); elems = defaultdict(list); rate = None; fl = 1024
    for ln in open(dump):
        t = ln.split()
        if not t: continue
        k = t[0]
        if k == "R": rate = int(t[1]); fl = int(t[2])
        elif k == "F": elems[int(t[1])].append([t[2], int(t[4]), []])
        elif k == "I":
            fr, ch = int(t[1]), int(t[2]); cur[(fr, ch)] = int(t[3]); wsq.setdefault(fr, {})[ch] = int(t[3])
            if elems[fr]: elems[fr][-1][2].append(ch)
        elif k == "B":
            bands[(int(t[1]), int(t[2]))][(int(t[3]), int(t[4]))] = (int(t[5]), int(t[6]), int(t[7]), int(t[9]))
        elif k == "E":
            grid.append([int(t[1]), int(t[2]), int(t[3]), int(t[4]), [int(x) for x in t[5].split(",")],
                         [int(x) for x in t[6].split(",")]])
    cnt = {}  # "wt|region|cat" -> n
    for fr, els in elems.items():
        for typ, msm, chs in els:
            if typ != "CPE" or len(chs) != 2: continue
            a, b = bands[(fr, chs[0])], bands[(fr, chs[1])]
            wt = "short" if cur[(fr, chs[0])] == 2 else "long"
            nl = fl // 8 if wt == "short" else fl
            for key, (s0, e0, cb0, ms) in a.items():
                if key not in b: continue
                cb1 = b[key][2]
                if cb0 in (14, 15) or cb1 in (14, 15): c = "is"
                elif cb0 == 13 or cb1 == 13: c = "pns"
                elif cb0 == 0 and cb1 == 0: c = "zero"
                elif ms: c = "ms"
                else: c = "lr"
                r = region(((s0 + e0) / 2.0) * rate / 2.0 / nl)
                kk = "%s|%d|%s" % (wt, r, c); cnt[kk] = cnt.get(kk, 0) + 1
    nfr = (max(elems) + 1) if elems else 0
    return dict(rate=rate, frame_len=fl, frames=nfr, grid=grid, stereo=cnt, ws=[[fr] + [wsq[fr].get(c, -1) for c in (0, 1)] for fr in sorted(wsq)])

def main():
    out = sys.argv[1]; os.makedirs(out, exist_ok=True)
    clips = sorted(os.path.basename(p) for p in glob.glob(AUDIO + "/*.wav"))
    tmp = tempfile.mkdtemp(dir="/home/user/work")
    src = {}
    for enc in ("faac", "apple", "fdk", "xaac"):
        for r in (32, 40, 48):
            for c in clips:
                of = "%s/%s%d|%s.json" % (out, enc, r, c)
                if os.path.exists(of): continue
                p = path(enc, r, c)
                if not os.path.exists(p): print("missing", p); continue
                if c not in src:
                    x, sr = sf.read(AUDIO + "/" + c, dtype="float64", always_2d=True); src = {c: mono48(x, sr)}
                subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", p, "-c", "copy", "-f", "adts", tmp + "/o.aac"], check=True)
                env = dict(os.environ, FAAD_DECDUMP=tmp + "/d.txt")
                subprocess.run([BIN, "-q", "--overwrite", "-o", tmp + "/o.wav", tmp + "/o.aac"], check=True, env=env)
                x, sr = sf.read(tmp + "/o.wav", dtype="float64", always_2d=True)
                res = parse(tmp + "/d.txt")
                res.update(lag48=est_lag(src[c], mono48(x, sr)), out_rate=int(sr), len=len(x))
                json.dump(res, open(of, "w"))
        print(enc, "done", flush=True)
main()
