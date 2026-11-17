#!/usr/bin/env python3
"""Grid syntax/continuity check on decoded E records (sbrgrid_run.py caches).
Per channel stream of frames: borders strictly increasing, num_env <= 4 (5 is the ISO cap),
t_E[last] <= 16 + 3, and the tiling rule: t_E[0] == t_E_prev[last] - 16 (0 when the previous
frame ended at 16), which also forbids FIXFIX/FIXVAR after a trailing border > 16.
Frame-class transition counts are returned for every ordered pair.
usage: sbrgrid_check.py DIR TAG CLIPLIST"""
import json, sys
CLS = ["FIXFIX", "FIXVAR", "VARFIX", "VARVAR"]

def check(path):
    r = json.load(open(path)); last = {}; bad = []; trans = {}
    for fr, ch, cls, le, te, fres in r["grid"]:
        if le > 4 or len(te) != le + 1 or te[-1] > 19 or any(b <= a for a, b in zip(te, te[1:])):
            bad.append((fr, ch, "limits", cls, te))
        if ch in last:
            pc, pe = last[ch]
            if te[0] != max(0, pe - 16): bad.append((fr, ch, "lead", cls, te, pe))
            k = CLS[pc] + ">" + CLS[cls]; trans[k] = trans.get(k, 0) + 1
        elif te[0] != 0: bad.append((fr, ch, "first", cls, te))
        last[ch] = (cls, te[-1])
    return bad, trans

if __name__ == "__main__":
    d, tag, lst = sys.argv[1:4]
    tot = {}; nbad = 0
    for clip in [l.strip() for l in open(lst) if l.strip()]:
        bad, tr = check("%s/%s|%s.json" % (d, tag, clip))
        nbad += len(bad)
        for k, v in tr.items(): tot[k] = tot.get(k, 0) + v
        for b in bad[:3]: print(clip, b)
    print("violations", nbad); print(json.dumps(dict(sorted(tot.items()))))
