"""Summarize adjusted S8-L arms. Usage: s8l_summary.py 96|128."""
import json
import math
import pathlib
import statistics as st
import sys

rate = sys.argv[1]
if rate not in ("96", "128"):
    raise SystemExit("rate must be 96 or 128")
lo, hi = ("80", "112") if rate == "96" else ("112", "144")
root = pathlib.Path(__file__).resolve().parents[2] / "results/s8l"
scores = {}
for arm in ("rSFr", "rSFr0", "rSFr1"):
    path = root / f"{arm}_{rate}k.json"
    if not path.exists():
        continue
    rows = json.loads(path.read_text())
    values = []
    bytes_base = bytes_arm = 0
    flags = []
    for row in rows.values():
        if arm not in row or "base" + lo not in row or "base" + hi not in row:
            continue
        base = row[f"base{rate}"]
        trial = row[arm]
        slope = (row[f"base{hi}"]["mos"] - row[f"base{lo}"]["mos"]) / math.log2(row[f"base{hi}"]["bytes"] / row[f"base{lo}"]["bytes"])
        adj = trial["mos"] - base["mos"] - slope * math.log2(trial["bytes"] / base["bytes"])
        values.append((adj, row["stem"]))
        bytes_base += base["bytes"]
        bytes_arm += trial["bytes"]
        if abs(trial["bytes"] / base["bytes"] - 1) > .125:
            flags.append(row["stem"])
    if not values:
        continue
    x = [v for v, _ in values]
    scores[arm] = st.mean(x)
    print(f"{arm} {len(x)}/49 adj {scores[arm]:+.4f} median {st.median(x):+.4f} W/L {sum(v > .0005 for v in x)}/{sum(v < -.0005 for v in x)} bytes {100*(bytes_arm/bytes_base-1):+.2f}%")
    print("  worst:", ", ".join(f"{name} {v:+.4f}" for v, name in sorted(values)[:5]))
    print("  byte flags:", ", ".join(flags) if flags else "none")
for arm in ("rSFr0", "rSFr1"):
    if arm in scores and "rSFr" in scores and scores["rSFr"]:
        print(f"{arm} share of rSFr: {100*scores[arm]/scores['rSFr']:.1f}%")
