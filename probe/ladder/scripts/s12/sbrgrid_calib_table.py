#!/usr/bin/env python3
"""T x L calibration table (HE-v1 40k CBR) and the prereg pick: max within-2 share subject
to non-FIXFIX frames <= 40 %. usage: sbrgrid_calib_table.py DIR CLIPLIST [out.json]"""
import sys, json, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sbrgrid_metrics import metrics
d, lst = sys.argv[1:3]
clips = [l.strip() for l in open(lst) if l.strip()]
rows = []
print("| T | L | onsets | within 2 % | within 1 % | FIXFIX at onset % | non-FIXFIX frames % |\n|---|---|---|---|---|---|---|")
for T in (2, 3, 4, 6, 8):
    for L in range(4):
        m = metrics(d, "T%dL%d" % (T, L), clips)
        rows.append(dict(T=T, L=L, **{k: m[k] for k in ("n", "near2", "near1", "fixfix_at_onset", "nonfixfix", "frames")}))
        print("| %d | %d | %d | %.2f | %.2f | %.2f | %.2f |" % (T, L, m["n"], 100 * m["near2"], 100 * m["near1"],
              100 * m["fixfix_at_onset"], 100 * m["nonfixfix"]))
ok = [r for r in rows if r["nonfixfix"] <= 0.40]
best = max(ok, key=lambda r: (r["near2"], -r["nonfixfix"]))
print("\nchosen (max within-2 with non-FIXFIX <= 40 %%, ties to fewer non-FIXFIX): T=%d L=%d" % (best["T"], best["L"]))
if len(sys.argv) > 3: json.dump(dict(rows=rows, chosen=best), open(sys.argv[3], "w"), indent=1)
