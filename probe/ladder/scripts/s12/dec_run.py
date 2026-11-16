#!/usr/bin/env python3
"""Dump decisions for all (encoder, variant) streams x clips, aggregate with decstats,
keep per-frame sequences (pickle, outside repo). Single process.
usage: dec_run.py <out_stats.json> <seq_pickle>
"""
import os, sys, glob, json, pickle, subprocess, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import decstats as D

AUDIO = os.environ["AUDIO"]
REF = "/home/user/faac/probe/ladder/ref"
DS = "/home/user/work/dec_streams"
clips = sorted(os.path.basename(p) for p in glob.glob(AUDIO + "/*.wav"))
V = {}  # key -> (encoder, profile, rung, path_fn)
for v in ("abr96", "abr128", "abr192", "ms96", "ms128", "ms192", "vbr160", "vbr342", "he32", "he48"):
    V["faac-" + v] = lambda c, v=v: "%s/%s/%s.m4a" % (DS, v, c)
for enc in ("apple", "fdk"):
    for prof, rung in (("lc", 128), ("lc", 192), ("he", 32), ("he", 48)):
        V["%s-%s%d" % (enc, prof, rung)] = lambda c, e=enc, p=prof, r=rung: "%s/%s-48k/%s/%dk/%s.m4a" % (REF, e, p, r, c)

out, seqs = {}, {}
tmp = tempfile.mkdtemp(dir="/home/user/work")
for key, fn in V.items():
    agg = D.new_stats(); per = {}; seqs[key] = {}
    for c in clips:
        p = fn(c)
        if not os.path.exists(p):
            print("MISSING", p, file=sys.stderr); continue
        dp = tmp + "/d.txt"
        subprocess.run([os.path.join(os.path.dirname(os.path.abspath(__file__)), "decdump.sh"), p, dp], check=True)
        st, seq = D.parse(dp)
        os.remove(dp)
        ics = st["ics"]
        per[c] = {"frames": st["frames"], "rate": st["rate"],
                  "short_share_ics": (ics["short"] / (ics["short"] + ics["long"])),
                  "short_ics": ics["short"], "ics": ics["short"] + ics["long"]}
        seqs[key][c] = {"rate": st["rate"], "frame_len": st["frame_len"], "seq": seq}
        D.merge(agg, st)
        if agg["rate"] is None: agg["rate"] = st["rate"]
    out[key] = {"aggregate": D.to_json(agg), "per_clip": per}
    print(key, "done", flush=True)
json.dump(out, open(sys.argv[1], "w"))
pickle.dump(seqs, open(sys.argv[2], "wb"))
