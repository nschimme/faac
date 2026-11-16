#!/usr/bin/env python3
"""Build stereo_shares.md/.json (+ per-clip short-share CSV) from dec_run.py raw output."""
import json, sys, csv
raw = json.load(open(sys.argv[1])); outdir = sys.argv[2]
R = ["0-2", "2-4", "4-6", "6-8", "8-12", "12-16", "16-24"]
C = ["ms", "is", "pns", "zero", "lr"]
KIND = {"abr": "ABR", "ms": "ABR", "vbr": "VBR", "he": "ABR"}
def desc(k):
    e, v = k.split("-", 1)
    if e == "faac":
        if v.startswith("ms"): return "FAAC LC ABR %sk --joint ms" % v[2:]
        if v.startswith("abr"): return "FAAC LC ABR %sk default stereo (auto)" % v[3:]
        if v.startswith("vbr"): return "FAAC LC VBR -q %s" % v[3:]
        return "FAAC HE-AAC v1 ABR %sk" % v[2:]
    return "%s %s %sk" % (e.capitalize() if e == "apple" else "FDK", v[:2].upper() if v[:2]=="lc" else "HE", v[2:])
def sh(cnt):
    n = sum(cnt[c] for c in C)
    return n, {c: (cnt[c] / n if n else None) for c in C}
def agg(cells):
    t = {c: sum(x[c] for x in cells) for c in C}
    return t
res = {}
md = ["# Stereo-tool shares (CPE pair bands) by encoder / variant", "",
      "Source: libfaad decision dump (`decdump.sh`) + `decstats.py` classification, aggregated over all 77 clips (48 kHz stereo).",
      "Per CPE pair band priority: IS (cb14/15 either ch) > PNS (cb13) > zero (both cb0) > M/S (mask) > L/R. Shares are over CPE pair bands only (SCE/mono bands excluded).",
      "Regions = band centre at the core rate, kHz. HE rows: AAC core only spans to core_rate/2 (empty regions above).", "",
      "**Rate control**: FAAC rows `abr*`, `ms*` and `he*` are ABR (`-b`); `vbr160`/`vbr342` are VBR (`-q`). Apple/FDK rows are the reference ladder streams at the given nominal rung (Apple: its ABR/CBR-like encoder; FDK: CBR packaged).",
      "FAAC default stereo mode = auto (per --joint help: 3=auto); `ms*` = `--joint ms`.", ""]
# summary table
md += ["## Overall (all regions)", "", "| stream | rate ctrl | CPE bands | M/S | IS | PNS | zero | L/R | short-win ICS % | TNS % long | TNS % short |",
       "|---|---|---|---|---|---|---|---|---|---|---|"]
def pct(x): return "-" if x is None else "%.1f" % (100 * x)
for k, v in raw.items():
    a = v["aggregate"]; bs = a["band_stats"]
    row = {}
    res[k] = {"desc": desc(k), "rate_control": "VBR" if k.startswith("faac-vbr") else ("ABR" if k.startswith("faac") else "ref"),
              "frames": a["frames"], "bands": {}, "short_frame_share_per_ch": a["short_frame_share"],
              "ics_count": a["ics_count"], "tns": a["tns"], "per_clip": v["per_clip"]}
    for w in ("long", "short"):
        res[k]["bands"][w] = {}
        for r in R:
            n, s = sh(bs[w][r]["counts"])
            res[k]["bands"][w][r] = {"n_cpe_bands": n, "share": s, "counts": {c: bs[w][r]["counts"][c] for c in C}}
        tot = agg([bs[w][r]["counts"] for r in R]); n, s = sh(tot)
        res[k]["bands"][w]["all"] = {"n_cpe_bands": n, "share": s, "counts": tot}
    tot_s = sum(x["short_ics"] for x in v["per_clip"].values()); tot_i = sum(x["ics"] for x in v["per_clip"].values())
    res[k]["short_ics_share"] = tot_s / tot_i
    L = res[k]["bands"]["long"]["all"]; Sx = res[k]["bands"]["short"]["all"]
    allc = agg([L["counts"], Sx["counts"]]); n, s = sh(allc)
    res[k]["bands"]["both"] = {"n_cpe_bands": n, "share": s}
    md.append("| %s | %s | %d | %s | %s | %s | %s |" % (desc(k), res[k]["rate_control"], n, " | ".join(pct(s[c]) for c in C),
        pct(res[k]["short_ics_share"]), pct(a["tns"]["long"]["tns_share"]), pct(a["tns"].get("short", {}).get("tns_share"))))
md += ["", "(M/S..L/R columns in the overall table are over ALL CPE bands, long+short.)", ""]
for k, v in res.items():
    md += ["## %s (`%s`, %s)" % (v["desc"], k, v["rate_control"]), "",
           "frames=%d, short-window ICS share %s%%, per-channel %s" % (v["frames"], pct(v["short_ics_share"]),
           {c: pct(x) for c, x in v["short_frame_share_per_ch"].items()})]
    md += ["TNS: long %s%% of %d ICS, short %s%% of %d ICS" % (pct(v["tns"]["long"]["tns_share"]), v["tns"]["long"]["ics"],
           pct(v["tns"].get("short", {}).get("tns_share")), v["tns"].get("short", {}).get("ics", 0)), ""]
    for w in ("long", "short"):
        md += ["%s windows" % w, "", "| kHz | n CPE bands | M/S | IS | PNS | zero | L/R |", "|---|---|---|---|---|---|---|"]
        for r in R + ["all"]:
            b = v["bands"][w][r]
            md.append("| %s | %d | %s |" % (r, b["n_cpe_bands"], " | ".join(pct(b["share"][c]) for c in C)))
        md.append("")
json.dump(res, open(outdir + "/stereo_shares.json", "w"), indent=1)
# per-clip short share csv
keys = list(raw)
clips = sorted(next(iter(raw.values()))["per_clip"])
with open(outdir + "/short_share_per_clip.csv", "w", newline="") as fh:
    w = csv.writer(fh); w.writerow(["clip"] + keys)
    for c in clips:
        w.writerow([c] + ["%.4f" % raw[k]["per_clip"][c]["short_share_ics"] for k in keys])
md += ["## Per-clip short-window share (ICS share, EIGHT_SHORT)", "",
       "Full table for all %d streams/variants: `short_share_per_clip.csv` (also in `stereo_shares.json` under `per_clip`)." % len(keys), "",
       "| clip | " + " | ".join(k for k in ("faac-abr128", "apple-lc128", "fdk-lc128", "faac-abr192", "apple-lc192", "fdk-lc192")) + " |", "|---|---|---|---|---|---|---|"]
for c in clips:
    md.append("| %s | %s |" % (c, " | ".join(pct(raw[k]["per_clip"][c]["short_share_ics"]) for k in ("faac-abr128", "apple-lc128", "fdk-lc128", "faac-abr192", "apple-lc192", "fdk-lc192"))))
open(outdir + "/stereo_shares.md", "w").write("\n".join(md) + "\n")
