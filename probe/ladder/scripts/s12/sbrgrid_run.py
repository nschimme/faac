#!/usr/bin/env python3
"""Encode clips with the SBR-grid arm, dump the decoded grid (E records of the in-tree
instrumented libfaad) and the xcorr lag, cache one json per (tag, clip).
usage: sbrgrid_run.py OUTDIR CLIPLIST TAG ENCODER 'faac args' [ENV=VAL ...]
tag names the cache (e.g. T4L1_he40); the faac args select profile/rate."""
import os, sys, json, subprocess, tempfile, shlex
from math import gcd
import numpy as np, soundfile as sf, scipy.signal as ss
AUDIO = os.environ["AUDIO"]
DUMP = os.environ.get("DECDUMP_BIN", "/home/user/work/faad-dump/build-rel/frontend/faad")

def mono48(x, sr):
    m = x.sum(1)
    if sr != 48000:
        g = gcd(sr, 48000); m = ss.resample_poly(m, 48000 // g, sr // g)
    return m

def est_lag(src, dec, n=480000):
    a = src[:n]; b = dec[:n + 16384]
    c = ss.fftconvolve(b, a[::-1], mode="full")
    return int(np.argmax(np.abs(c))) - (len(a) - 1)

def parse_grid(dump):
    grid = []  # frame, ch, class, L_E, t_E, freq_res
    for ln in open(dump):
        if ln[:2] != "E ": continue
        t = ln.split()
        grid.append([int(t[1]), int(t[2]), int(t[3]), int(t[4]),
                     [int(x) for x in t[5].split(",")], [int(x) for x in t[6].split(",")]])
    return grid

def run_one(enc, args, clip, env_extra, tmp, keep=None):
    m4a = keep or tmp + "/o.m4a"
    env = dict(os.environ, **env_extra)
    subprocess.run([enc, "--overwrite"] + args + ["-o", m4a, AUDIO + "/" + clip], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env)
    denv = dict(os.environ, FAAD_DECDUMP=tmp + "/d.txt")
    subprocess.run([DUMP, "-q", "--overwrite", "-o", tmp + "/o.wav", m4a], check=True, env=denv)
    x, sr = sf.read(tmp + "/o.wav", dtype="float64", always_2d=True)
    s, ssr = sf.read(AUDIO + "/" + clip, dtype="float64", always_2d=True)
    return dict(grid=parse_grid(tmp + "/d.txt"), lag48=est_lag(mono48(s, ssr), mono48(x, sr)),
                out_rate=int(sr), len=len(x), bytes=os.path.getsize(m4a))

def main():
    out, lst, tag, enc, args = sys.argv[1:6]
    env_extra = dict(a.split("=", 1) for a in sys.argv[6:])
    os.makedirs(out, exist_ok=True)
    tmp = tempfile.mkdtemp(dir="/home/user/work")
    for clip in [l.strip() for l in open(lst) if l.strip()]:
        of = "%s/%s|%s.json" % (out, tag, clip)
        if os.path.exists(of): continue
        res = run_one(enc, shlex.split(args), clip, env_extra, tmp)
        json.dump(res, open(of, "w"))
    subprocess.run(["rm", "-rf", tmp])
main()
