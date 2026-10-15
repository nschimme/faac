#!/usr/bin/env python3
"""Create a review report from run_features.py JSON output; does not run codecs."""

from __future__ import annotations

import argparse
import json
import math
import os
import statistics
import sys
from pathlib import Path

BENCH = Path("/Users/nschimme/gitprojects/faac-benchmark")
DEFAULT_OUTPUT = Path("/private/tmp/faac-he-v1-probes/probe/results-gate")
BASELINE = "baseline_default"
CONTROL_VARIANTS = ["candidate_default", "defaults_explicit"]


def fmt(value, digits=4, suffix=""):
    if value is None or not math.isfinite(float(value)):
        return "n/a"
    return f"{float(value):+.{digits}f}{suffix}"


def get_row(rows, variant, clip, rate):
    key = f"{variant}|{clip}|{rate}"
    if key not in rows:
        raise KeyError(f"Missing row {key}")
    return rows[key]


def mean_ci(values, bootstrap_mean_ci):
    vals = [float(v) for v in values if v is not None and math.isfinite(float(v))]
    if not vals:
        return {"n": 0, "mean": None, "lo": None, "hi": None}
    ci = bootstrap_mean_ci(vals)
    return {
        "n": len(vals), "mean": statistics.mean(vals),
        "lo": ci["lo"] if ci else None, "hi": ci["hi"] if ci else None,
    }


def median_ci(values):
    """Bootstrap a median by resampling clip values with the suite's seeded RNG."""
    vals = [float(v) for v in values if v is not None and math.isfinite(float(v))]
    if not vals:
        return {"median": None, "lo": None, "hi": None}
    import random
    rng = random.Random(0)
    draws = sorted(statistics.median(rng.choice(vals) for _ in vals) for _ in range(2000))
    return {"median": statistics.median(vals), "lo": draws[49], "hi": draws[1949]}


def classify(ci):
    if ci["lo"] is None or ci["hi"] is None:
        return "insufficient valid clips"
    if ci["hi"] < 0:
        return "aggregate BD-rate CI below zero: statistically improves this benchmark"
    if ci["lo"] <= 0 <= ci["hi"]:
        return "aggregate BD-rate CI crosses zero: inconclusive"
    return "aggregate BD-rate CI above zero: regresses on this benchmark"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT,
                    help="runner output directory (gate/full output dirs are separate)")
    ap.add_argument("--results", type=Path, help="override feature-results.json")
    ap.add_argument("--summary", type=Path, help="override feature-summary.json")
    ap.add_argument("--output", type=Path, help="output Markdown path (default: OUTPUT_DIR/RESULTS.md)")
    args = ap.parse_args()

    os.environ.setdefault("NUMBA_DISABLE_JIT", "1")
    sys.path.insert(0, str(BENCH))
    from compare_results import bootstrap_mean_ci

    results_path = args.results or args.output_dir / "feature-results.json"
    summary_path = args.summary or args.output_dir / "feature-summary.json"
    config_path = args.output_dir / "feature-config.json"
    report_path = args.output or args.output_dir / "RESULTS.md"
    results = json.loads(results_path.read_text())
    summary = json.loads(summary_path.read_text())
    rows = results["rows"]
    clips = results.get("clips") or sorted({r["clip"] for r in rows.values()})
    rates = results.get("rates_kbps") or sorted({int(r["target_kbps"]) for r in rows.values()})
    if not clips or not rates:
        raise RuntimeError("Result file has no clips or bitrate rungs")
    variants = list(dict.fromkeys(CONTROL_VARIANTS + list(summary.get("variants", {}))))
    if not variants:
        variants = CONTROL_VARIANTS + sorted({r["variant"] for r in rows.values()} - {BASELINE})

    report = [
        "# HE-AAC v1 SBR probe results", "",
        f"Input: `{results_path}`; runner summary: `{summary_path}`.",
        f"Corpus: **{len(clips)} clips**; bitrate rungs: {', '.join(f'{r}k' for r in rates)}.",
        "",
        "BD-rate is computed by the benchmark's PCHIP helper from actual AAC packet payload rates and Zimtohrli MOS. Per-clip BD-rate values are the independent units for bootstrap intervals; bitrate rungs are not treated as independent samples.",
        "",
        "## Per-variant assessment", "",
        "| Variant | Packet identities changed | Valid BD clips | Mean BD-rate (95% bootstrap CI) | Median BD-rate (95% bootstrap CI) | Assessment |",
        "|---|---:|---:|---:|---:|---|",
    ]
    variant_stats = {}
    for variant in variants:
        bd_info = summary.get("variants", {}).get(variant, {})
        per_clip = bd_info.get("per_clip", [])
        bd_values = [p.get("bd_rate_percent") for p in per_clip]
        valid_bd = [float(v) for v in bd_values if v is not None and math.isfinite(float(v))]
        mean_stat = mean_ci(valid_bd, bootstrap_mean_ci)
        med_stat = median_ci(valid_bd)
        changed = 0
        compared = 0
        for clip in clips:
            for rate in rates:
                base = get_row(rows, BASELINE, clip, rate)
                candidate = get_row(rows, variant, clip, rate)
                compared += 1
                changed += base.get("payload_sha256") != candidate.get("payload_sha256")
        assess = classify(mean_stat)
        variant_stats[variant] = {
            "mean_bd": mean_stat, "median_bd": med_stat,
            "packet_identity_changed": changed, "packet_identity_compared": compared,
            "valid_bd_count": len(valid_bd), "assessment": assess,
        }
        report.append(
            f"| {variant} | {changed}/{compared} | {len(valid_bd)}/{len(clips)} | "
            f"{fmt(mean_stat['mean'], 3, '%')} [{fmt(mean_stat['lo'], 3, '%')}, {fmt(mean_stat['hi'], 3, '%')}] | "
            f"{fmt(med_stat['median'], 3, '%')} [{fmt(med_stat['lo'], 3, '%')}, {fmt(med_stat['hi'], 3, '%')}] | {assess} |"
        )

    report.extend(["", "Mean BD-rate classification follows the 95% bootstrap CI: fully below zero is an improvement on this benchmark, crossing zero is inconclusive, and fully above zero is a regression. This is a screening result, not a shipping verdict.", "",
                   "## Paired per-rate results", "",
                   "MOS values and actual payload rates are averaged across the same clips. Confidence intervals use paired per-clip MOS deltas; the four bitrate rungs are analyzed separately.", "",
                   "| Variant | Target | Clips | Baseline MOS | Candidate MOS | MOS Δ (95% paired bootstrap CI) | Actual payload Δ |",
                   "|---|---:|---:|---:|---:|---:|---:|"])
    for variant in variants:
        for rate in rates:
            base_mos, cand_mos, mos_delta, rate_delta, rate_pct = [], [], [], [], []
            for clip in clips:
                base = get_row(rows, BASELINE, clip, rate)
                cand = get_row(rows, variant, clip, rate)
                base_mos.append(float(base["mos"]))
                cand_mos.append(float(cand["mos"]))
                mos_delta.append(float(cand["mos"]) - float(base["mos"]))
                rate_delta.append(float(cand["actual_payload_kbps"]) - float(base["actual_payload_kbps"]))
                rate_pct.append(100.0 * (float(cand["actual_payload_kbps"]) / float(base["actual_payload_kbps"]) - 1.0))
            ci = mean_ci(mos_delta, bootstrap_mean_ci)
            report.append(
                f"| {variant} | {rate} kbps | {len(clips)} | {statistics.mean(base_mos):.5f} | {statistics.mean(cand_mos):.5f} | "
                f"{fmt(ci['mean'], 5)} [{fmt(ci['lo'], 5)}, {fmt(ci['hi'], 5)}] | "
                f"{statistics.mean(rate_delta):+.3f} kbps ({statistics.mean(rate_pct):+.3f}%) |"
            )

    report.extend(["", "## Best and worst clips", "",
                   "For MOS, the table shows the largest and smallest per-clip paired MOS delta at each target rung. BD-rate uses the complete ladder per clip.", "",
                   "| Variant | Best BD clip | Best BD-rate | Worst BD clip | Worst BD-rate | Best MOS clip | MOS Δ | Worst MOS clip | MOS Δ |",
                   "|---|---|---:|---|---:|---|---:|---|---:|"])
    for variant in variants:
        per_clip_bd = summary.get("variants", {}).get(variant, {}).get("per_clip", [])
        valid = [p for p in per_clip_bd if p.get("bd_rate_percent") is not None]
        if valid:
            best_bd = min(valid, key=lambda x: float(x["bd_rate_percent"]))
            worst_bd = max(valid, key=lambda x: float(x["bd_rate_percent"]))
            bd_cells = (best_bd["clip"], fmt(best_bd["bd_rate_percent"], 3, "%"),
                        worst_bd["clip"], fmt(worst_bd["bd_rate_percent"], 3, "%"))
        else:
            bd_cells = ("n/a", "n/a", "n/a", "n/a")
        all_mos = []
        for clip in clips:
            for rate in rates:
                base = get_row(rows, BASELINE, clip, rate)
                candidate = get_row(rows, variant, clip, rate)
                all_mos.append((float(candidate["mos"]) - float(base["mos"]), clip, rate))
        mos_best = max(all_mos)
        mos_worst = min(all_mos)
        report.append(
            f"| {variant} | {bd_cells[0]} | {bd_cells[1]} | {bd_cells[2]} | {bd_cells[3]} | "
            f"{mos_best[1]} @ {mos_best[2]}k | {mos_best[0]:+.5f} | {mos_worst[1]} @ {mos_worst[2]}k | {mos_worst[0]:+.5f} |"
        )

    report.extend(["", "## Provenance and scope", "",
                   f"- Results contain {len(clips)} clips and {len(rates)} target rates; gate is expected to be n=4, full corpus n=49 when the selected corpus has those counts.",
                   f"- Probe source diff SHA-256: `{json.loads(config_path.read_text()).get('source_diff_sha256', 'unavailable') if config_path.is_file() else 'feature-config.json unavailable'}`.",
                   "- Size and speed were not tested; both remain pending.",
                   "- No result here establishes shipping readiness. Review per-clip outcomes, quality/rate curves, and follow-up size/speed gates.", ""])
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text("\n".join(report))
    print(f"Wrote {report_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
