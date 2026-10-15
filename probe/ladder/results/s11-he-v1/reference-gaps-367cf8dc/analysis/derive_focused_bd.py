#!/usr/bin/env python3
"""Recompute the four recorded equal-output-format focus BD summaries.

Usage: python3 analysis/derive_focused_bd.py --bench-root /path/to/faac-benchmark
Run from this package directory. No files in the benchmark checkout are written.
"""
import argparse
import gzip
import hashlib
import json
import math
import sys
from pathlib import Path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bench-root", required=True, type=Path,
                    help="faac-benchmark checkout providing bd_rate.py and compare_results.py")
    ap.add_argument("--package-root", type=Path, default=Path(__file__).resolve().parent.parent,
                    help="reference-gaps-367cf8dc package directory")
    ap.add_argument("--output", type=Path, default=Path("focused-bd-recomputed.json"))
    args = ap.parse_args()
    pkg, bench = args.package_root.resolve(), args.bench_root.resolve()
    sys.path[:0] = [str(bench), str(bench / "scripts")]
    from bd_rate import bd_rate_curve
    from compare_results import bootstrap_mean_ci

    manifest = json.loads((pkg / "manifest.json").read_text())
    raw = manifest["raw_inputs"]["comparison_results.json"]
    compressed = (pkg / raw["compressed_path"]).read_bytes()
    if hashlib.sha256(compressed).hexdigest() != raw["compressed_sha256"]:
        raise SystemExit("compressed encoder matrix hash mismatch")
    blob = gzip.decompress(compressed)
    if len(blob) != raw["original_bytes"] or hashlib.sha256(blob).hexdigest() != raw["original_sha256"]:
        raise SystemExit("uncompressed encoder matrix hash mismatch")
    rows = json.loads(blob)
    streams = json.loads((pkg / "analysis/apple48-stream-validation.json").read_text())
    apple48 = {(x["row_key"], x["scenario"], x["filename"])
               for x in streams if len(x.get("stream", [])) == 1
               and x["stream"][0].get("sample_rate") == "48000"
               and x["stream"][0].get("channels") == 2
               and x["stream"][0].get("profile") in ("LC", "HE-AAC")}
    faac_tool = "FAAC 3.0.0 (367cf8dc)"
    data = {}
    for r in rows:
        if not r.get("decode_valid") or r.get("mos") is None or not math.isfinite(r["mos"]):
            continue
        if not isinstance(r.get("actual_bitrate"), (int, float)) or r["actual_bitrate"] <= 0:
            continue
        if r["tool"].startswith("Apple") and (r["row_key"], r["scenario"], r["filename"]) not in apple48:
            continue
        data[(r["tool"], r["profile"], r["scenario"], r["filename"])] = r

    results = {}
    for profile, rungs in (("he", [40, 48, 56, 64]),
                           ("lc", [128, 160, 192, 256, 320])):
        for reference in ("fdkaac 1.0.9", "Apple AAC 27.0.1"):
            clips = sorted({r["filename"] for r in rows
                            if r["scenario"] == f"48k_stereo_{rungs[0]}k"})
            per_clip, excluded = [], []
            for clip in clips:
                a = [data.get((faac_tool, profile, f"48k_stereo_{rate}k", clip)) for rate in rungs]
                b = [data.get((reference, profile, f"48k_stereo_{rate}k", clip)) for rate in rungs]
                if any(x is None for x in a + b):
                    excluded.append({"clip": clip, "reason": "missing or invalid matched row"})
                    continue
                value = bd_rate_curve(
                    [(x["actual_bitrate"], x["mos"]) for x in b],
                    [(x["actual_bitrate"], x["mos"]) for x in a])
                if value is None or not math.isfinite(value):
                    excluded.append({"clip": clip, "reason": "non-overlapping or invalid quality curves"})
                    continue
                per_clip.append({"clip": clip, "faac_bd_percent": value})
            vals = [x["faac_bd_percent"] for x in per_clip]
            results[f"{profile} vs {reference}"] = {
                "rungs_kbps": rungs, "n": len(vals),
                "faac_bd_percent_mean": sum(vals) / len(vals) if vals else None,
                "bootstrap_ci_95": bootstrap_mean_ci(vals, n=2000, seed=0) if vals else None,
                "excluded": excluded, "per_clip": per_clip,
            }
    args.output.write_text(json.dumps(results, indent=2, sort_keys=True) + "\n")
    print(f"wrote {args.output}; benchmark checkout was read-only")


if __name__ == "__main__":
    main()
