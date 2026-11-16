#!/usr/bin/env python3
"""Aggregate FAAD_DECDUMP files into encoder-decision statistics.

usage: decstats.py [--json out.json] [--md out.md] [--seq-dir DIR] dump...
Per stream and aggregated. Record format: see DECDUMP.md.
"""
import argparse, csv, json, os, sys
from collections import Counter, defaultdict

REGIONS = [(0, 2000), (2000, 4000), (4000, 6000), (6000, 8000),
           (8000, 12000), (12000, 16000), (16000, 24000)]
RNAMES = ["0-2", "2-4", "4-6", "6-8", "8-12", "12-16", "16-24"]
CATS = ["ms", "is", "pns", "zero", "lr", "mono_pns", "mono_zero", "mono_spec"]
WSEQ = {0: "ONLY_LONG", 1: "LONG_START", 2: "EIGHT_SHORT", 3: "LONG_STOP"}


def wtype(ws):
    return "short" if ws == 2 else "long"


def region(f):
    for i, (lo, hi) in enumerate(REGIONS):
        if lo <= f < hi:
            return i
    return len(REGIONS) - 1


def new_stats():
    return {
        "bands": {w: [{c: 0 for c in CATS} for _ in RNAMES] for w in ("long", "short")},
        "lines": {w: [{c: 0 for c in CATS} for _ in RNAMES] for w in ("long", "short")},
        "ics": Counter(), "ics_ch": defaultdict(Counter), "tns": defaultdict(Counter),
        "tns_nfilt": defaultdict(Counter), "tns_order": defaultdict(Counter),
        "elem": Counter(), "cpe_ms_mode": Counter(), "cpe_common": Counter(),
        "frames": 0, "rate": None, "frame_len": 1024,
        "sbr": defaultdict(Counter), "ps": defaultdict(Counter),
    }


def parse(path):
    st = new_stats()
    seq = []
    cur = {}  # (frame, ch) -> ics info
    bands = defaultdict(dict)  # (frame, ch) -> {(g,sfb): (s,e,cb,sf,ms)}
    pair_of = {}  # (frame, ch) -> partner ch
    frame_elem = {}  # frame -> list of (type, chs)
    last_elem = None
    maxf = -1
    with open(path) as fh:
        for ln in fh:
            t = ln.split()
            if not t:
                continue
            k = t[0]
            if k == "R":
                st["rate"] = int(t[1]); st["frame_len"] = int(t[2])
            elif k == "F":
                fr = int(t[1]); maxf = max(maxf, fr)
                last_elem = [t[2], int(t[3]), int(t[4]), fr, []]
                frame_elem.setdefault(fr, []).append(last_elem)
                st["elem"][t[2]] += 1
                if t[2] == "CPE":
                    st["cpe_common"][int(t[3])] += 1
                    st["cpe_ms_mode"][int(t[4])] += 1
            elif k == "I":
                fr, ch = int(t[1]), int(t[2])
                ws, wsh, msfb, ng = int(t[3]), int(t[4]), int(t[5]), int(t[6])
                tns = int(t[9]); nf = [int(x) for x in t[10].split(",")]
                info = dict(ws=ws, shape=wsh, max_sfb=msfb, ng=ng, gain=int(t[8]),
                            tns=tns, nf=nf, tnssum=t[11], pulse=int(t[12]), gc=int(t[13]))
                cur[(fr, ch)] = info
                if last_elem is not None and last_elem[3] == fr:
                    last_elem[4].append(ch)
                seq.append((fr, ch, ws, tns, sum(nf)))
                wt = wtype(ws)
                st["ics"][wt] += 1
                st["ics_ch"][ch][wt] += 1
                st["ics_ch"][ch]["all"] += 1
                st["tns"][wt]["total"] += 1
                st["tns"][wt]["on"] += tns
                if tns:
                    st["tns_nfilt"][wt][sum(nf)] += 1
                    for item in t[11].split("+"):
                        if item != "-":
                            o = int(item.split("/")[0].split(":o")[1])
                            st["tns_order"][wt][o] += 1
                st["ics"]["pulse"] += info["pulse"]
                st["ics"]["gc"] += info["gc"]
            elif k == "B":
                fr, ch, g, sfb = int(t[1]), int(t[2]), int(t[3]), int(t[4])
                bands[(fr, ch)][(g, sfb)] = (int(t[5]), int(t[6]), int(t[7]), int(t[8]), int(t[9]))
            elif k == "S":
                ch = int(t[2])
                s = st["sbr"]
                s["amp_res"][t[3]] += 1; s["start_freq"][t[4]] += 1
                s["stop_freq"][t[5]] += 1; s["xover_band"][t[6]] += 1
                s["freq_scale"][t[7]] += 1; s["num_env"][t[8]] += 1
                s["frame_class"][t[9]] += 1; s["num_noise"][t[10]] += 1
                for m in t[11].split(","):
                    if m != "":
                        s["invf_mode"][m] += 1
                s["add_harmonic"][t[12]] += 1; s["coupling"][t[13]] += 1
                s["records"][str(ch)] += 1
            elif k == "P":
                p = st["ps"]
                p["enable_iid"][t[2]] += 1; p["enable_icc"][t[3]] += 1
                p["num_env"][t[4]] += 1; p["iid_mode"][t[5]] += 1; p["icc_mode"][t[6]] += 1
                p["records"]["n"] += 1
    st["frames"] = maxf + 1
    fs = st["rate"] or 44100
    fl = st["frame_len"]
    for fr, els in frame_elem.items():
        for typ, cw, msm, _, chs in els:
            if typ == "CPE" and len(chs) == 2:
                a, b = bands[(fr, chs[0])], bands[(fr, chs[1])]
                info = cur[(fr, chs[0])]
                wt = wtype(info["ws"])
                nl = fl // 8 if wt == "short" else fl
                for key, (s0, e0, cb0, sf0, ms) in a.items():
                    if key not in b:
                        continue
                    cb1 = b[key][2]
                    if cb1 in (14, 15) or cb0 in (14, 15):
                        c = "is"
                    elif cb0 == 13 or cb1 == 13:
                        c = "pns"
                    elif cb0 == 0 and cb1 == 0:
                        c = "zero"
                    elif ms:
                        c = "ms"
                    else:
                        c = "lr"
                    r = region(((s0 + e0) / 2.0) * fs / 2.0 / nl)
                    st["bands"][wt][r][c] += 1
                    st["lines"][wt][r][c] += e0 - s0
            else:
                for ch in chs:
                    info = cur[(fr, ch)]
                    wt = wtype(info["ws"])
                    nl = fl // 8 if wt == "short" else fl
                    for key, (s0, e0, cb0, sf0, ms) in bands[(fr, ch)].items():
                        c = "mono_pns" if cb0 == 13 else "mono_zero" if cb0 == 0 else "mono_spec"
                        r = region(((s0 + e0) / 2.0) * fs / 2.0 / nl)
                        st["bands"][wt][r][c] += 1
                        st["lines"][wt][r][c] += e0 - s0
    return st, seq


def merge(a, b):
    for w in ("long", "short"):
        for r in range(len(RNAMES)):
            for key in ("bands", "lines"):
                for c in CATS:
                    a[key][w][r][c] += b[key][w][r][c]
    for key in ("ics", "elem", "cpe_ms_mode", "cpe_common"):
        a[key].update(b[key])
    for key in ("ics_ch", "tns", "tns_nfilt", "tns_order", "sbr", "ps"):
        for sub, cnt in b[key].items():
            a[key][sub].update(cnt)
    a["frames"] += b["frames"]


def shares(cnt):
    n = sum(cnt.values())
    return {c: (cnt[c] / n if n else None) for c in CATS}, n


def to_json(st):
    out = {"frames": st["frames"], "rate": st["rate"], "elements": dict(st["elem"]),
           "cpe_common_window": {str(k): v for k, v in st["cpe_common"].items()},
           "cpe_ms_mask_present": {str(k): v for k, v in st["cpe_ms_mode"].items()}}
    out["band_stats"] = {}
    for w in ("long", "short"):
        out["band_stats"][w] = {}
        for i, rn in enumerate(RNAMES):
            sh, n = shares(st["bands"][w][i])
            lsh, ln_ = shares(st["lines"][w][i])
            out["band_stats"][w][rn] = {"n_bands": n, "counts": st["bands"][w][i],
                                        "share": sh, "n_lines": ln_, "line_share": lsh}
    out["short_frame_share"] = {
        str(ch): (c["short"] / c["all"] if c["all"] else None) for ch, c in sorted(st["ics_ch"].items())}
    out["ics_count"] = {str(ch): dict(c) for ch, c in sorted(st["ics_ch"].items())}
    out["tns"] = {w: {"ics": c["total"], "tns_share": (c["on"] / c["total"] if c["total"] else None),
                      "nfilt_dist": {str(k): v for k, v in sorted(st["tns_nfilt"][w].items())},
                      "order_dist": {str(k): v for k, v in sorted(st["tns_order"][w].items())}}
                  for w, c in st["tns"].items()}
    out["pulse_ics"] = st["ics"]["pulse"]
    out["gain_control_ics"] = st["ics"]["gc"]
    if st["sbr"]:
        out["sbr"] = {k: dict(sorted(v.items())) for k, v in st["sbr"].items()}
        n = sum(st["sbr"]["records"].values())
        out["sbr_records"] = n
    if st["ps"]:
        out["ps"] = {k: dict(sorted(v.items())) for k, v in st["ps"].items()}
        n = st["ps"]["records"]["n"]
        out["ps_enable_iid_share"] = st["ps"]["enable_iid"]["1"] / n if n else None
        out["ps_enable_icc_share"] = st["ps"]["enable_icc"]["1"] / n if n else None
    return out


def pct(x):
    return "-" if x is None else "%.1f" % (100 * x)


def to_md(name, j):
    L = ["## %s" % name, "",
         "frames=%d rate=%s elements=%s cpe_ms_mask_present=%s" % (
             j["frames"], j["rate"], j["elements"], j["cpe_ms_mask_present"]), ""]
    for w in ("long", "short"):
        L += ["### band shares, %s windows (CPE pair bands; mono = SCE bands)" % w, "",
              "| kHz | n | M/S | IS | PNS | zero | L/R | mono spec | mono PNS | mono zero |",
              "|---|---|---|---|---|---|---|---|---|---|"]
        for rn in RNAMES:
            b = j["band_stats"][w][rn]
            s = b["share"]
            L.append("| %s | %d | %s |" % (rn, b["n_bands"], " | ".join(
                pct(s[c]) for c in ("ms", "is", "pns", "zero", "lr", "mono_spec", "mono_pns", "mono_zero"))))
        L.append("")
    L += ["### EIGHT_SHORT share per channel: " + ", ".join(
        "ch%s=%s%%" % (c, pct(v)) for c, v in j["short_frame_share"].items()), ""]
    L += ["### TNS", "", "| window | ics | TNS % | nfilt dist | order dist |", "|---|---|---|---|---|"]
    for w, t in j["tns"].items():
        L.append("| %s | %d | %s | %s | %s |" % (w, t["ics"], pct(t["tns_share"]), t["nfilt_dist"], t["order_dist"]))
    L.append("")
    if "sbr" in j:
        L += ["### SBR (%d records)" % j["sbr_records"], ""]
        for k, v in j["sbr"].items():
            L.append("- %s: %s" % (k, v))
        L.append("")
    if "ps" in j:
        L += ["### PS", ""]
        for k, v in j["ps"].items():
            L.append("- %s: %s" % (k, v))
        L.append("- enable_iid share %s%%, enable_icc share %s%%" % (
            pct(j["ps_enable_iid_share"]), pct(j["ps_enable_icc_share"])))
        L.append("")
    return "\n".join(L)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dumps", nargs="+")
    ap.add_argument("--json")
    ap.add_argument("--md")
    ap.add_argument("--seq-dir", help="write <name>.seq.csv: frame,ch,window_seq,tns_present,nfilt")
    a = ap.parse_args()
    agg = new_stats()
    res = {"streams": {}}
    md = []
    for p in a.dumps:
        name = os.path.basename(p)
        st, seq = parse(p)
        res["streams"][name] = to_json(st)
        md.append(to_md(name, res["streams"][name]))
        merge(agg, st)
        if agg["rate"] is None:
            agg["rate"] = st["rate"]
        if a.seq_dir:
            os.makedirs(a.seq_dir, exist_ok=True)
            with open(os.path.join(a.seq_dir, name + ".seq.csv"), "w", newline="") as fh:
                w = csv.writer(fh)
                w.writerow(["frame", "ch", "window_seq", "tns_present", "nfilt"])
                w.writerows(seq)
    if len(a.dumps) > 1:
        res["aggregate"] = to_json(agg)
        md.insert(0, to_md("AGGREGATE (%d streams; region by each stream's own rate)" % len(a.dumps),
                           res["aggregate"]))
    if a.json:
        with open(a.json, "w") as fh:
            json.dump(res, fh, indent=1)
    text = "\n".join(md)
    if a.md:
        with open(a.md, "w") as fh:
            fh.write(text + "\n")
    if not a.json and not a.md:
        print(text)


if __name__ == "__main__":
    main()
