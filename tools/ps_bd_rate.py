#!/usr/bin/env python3
"""Per-clip PCHIP BD-rate report for diagnostic PS quality manifests."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import random
import statistics
import subprocess
import sys


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def load(path):
    data = json.loads(Path(path).read_text())
    rows = data.get("results", data)
    if isinstance(rows, dict):
        rows = list(rows.values())
    if not isinstance(rows, list):
        raise ValueError(f"{path}: expected a results mapping or list")
    return rows


def sample_rate(row):
    audio = row.get("input_audio") or {}
    if audio.get("sample_rate"):
        return int(audio["sample_rate"])
    clip = row.get("clip") or row.get("input_path")
    if not clip or not Path(clip).is_file():
        return None
    proc = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "a:0", "-show_entries",
                           "stream=sample_rate", "-of", "default=noprint_wrappers=1:nokey=1", clip],
                          capture_output=True, text=True)
    try:
        return int(proc.stdout.strip()) if proc.returncode == 0 else None
    except ValueError:
        return None


def clip_key(row):
    return Path(row.get("clip") or row.get("input_path") or "").name, sample_rate(row)


def dedupe(rows):
    unique = {}
    for row in rows:
        path = row.get("input_path") or row.get("clip") or ""
        key = (row.get("binary_sha256"), str(Path(path).resolve()) if path else "",
               row.get("profile"), row.get("bitrate_kbps_total"),
               json.dumps(row.get("environment") or {}, sort_keys=True, separators=(",", ":")))
        if key not in unique or unique[key].get("status") != "ok" and row.get("status") == "ok":
            unique[key] = row
    return list(unique.values())


def pct(values, p):
    ordered = sorted(float(x) for x in values)
    if not ordered:
        return None
    at = (len(ordered) - 1) * p
    lo, hi = math.floor(at), math.ceil(at)
    return ordered[lo] + (ordered[hi] - ordered[lo]) * (at - lo)


def summarize(values):
    values = [float(x) for x in values]
    if not values:
        return {"n": 0, "mean": None, "median": None, "min": None, "p10": None,
                "max": None, "ci95": None}
    rng = random.Random(0)
    n = len(values)
    means = [statistics.fmean(rng.choice(values) for _ in range(n)) for _ in range(10000)]
    return {"n": n, "mean": statistics.fmean(values), "median": statistics.median(values),
            "min": min(values), "p10": pct(values, .10), "max": max(values),
            "ci95": [pct(means, .025), pct(means, .975)]}


def find_arm(rows, profile, bitrate):
    hits = [r for r in rows if r.get("profile") == profile and
            r.get("bitrate_kbps_total") == bitrate]
    good = [r for r in hits if r.get("status") == "ok" and r.get("ffmpeg_mos") is not None and
            r.get("payload_bitrate_kbps") is not None]
    return (good[0] if len(good) == 1 else None), len(hits)


def quality_span(points, monotone_points):
    mos, _ = monotone_points(points)
    return (float(mos[0]), float(mos[-1])) if len(mos) >= 2 else None


def cohort_report(base_groups, cand_groups, keys, baseline_profile, candidate_profile,
                  bitrates, bd_rate_curve, monotone_points, implausible_pct):
    per_clip = []
    counts = {"candidate_keys": len(keys), "matched": 0, "missing_points": 0,
              "insufficient_raw_points": 0, "monotone_invalid": 0,
              "no_quality_overlap": 0, "implausible_excluded": 0}
    for key in sorted(keys, key=lambda x: (x[0], x[1] if x[1] is not None else -1)):
        base_group, cand_group = base_groups.get(key, []), cand_groups.get(key, [])
        bp, cp, errors = [], [], []
        for rate in bitrates:
            base, bn = find_arm(base_group, baseline_profile, rate)
            cand, cn = find_arm(cand_group, candidate_profile, rate)
            if base is None or cand is None:
                errors.append({"bitrate_kbps": rate,
                               "baseline": "missing_or_error" if base is None else "ok",
                               "candidate": "missing_or_error" if cand is None else "ok",
                               "baseline_rows": bn, "candidate_rows": cn})
                continue
            bp.append((float(base["payload_bitrate_kbps"]), float(base["ffmpeg_mos"])))
            cp.append((float(cand["payload_bitrate_kbps"]), float(cand["ffmpeg_mos"])))
        record = {"clip": key[0], "input_sample_rate": key[1], "nominal_bitrates_kbps": list(bitrates),
                  "baseline_points": [{"payload_bitrate_kbps": r, "ffmpeg_mos": m} for r, m in bp],
                  "candidate_points": [{"payload_bitrate_kbps": r, "ffmpeg_mos": m} for r, m in cp],
                  "missing_points": errors}
        if errors:
            counts["missing_points"] += 1
            per_clip.append(record)
            continue
        if len(bp) < 3 or len(cp) < 3:
            counts["insufficient_raw_points"] += 1
            per_clip.append(record)
            continue
        bspan = quality_span(bp, monotone_points)
        cspan = quality_span(cp, monotone_points)
        if bspan is None or cspan is None:
            counts["monotone_invalid"] += 1
            per_clip.append(record)
            continue
        lo, hi = max(bspan[0], cspan[0]), min(bspan[1], cspan[1])
        if hi <= lo:
            counts["no_quality_overlap"] += 1
            per_clip.append(record)
            continue
        value = bd_rate_curve(bp, cp)
        if value is None:
            counts["no_quality_overlap"] += 1
            per_clip.append(record)
            continue
        if abs(value) > implausible_pct:
            counts["implausible_excluded"] += 1
            record["bd_rate_percent_excluded"] = value
            per_clip.append(record)
            continue
        counts["matched"] += 1
        record.update(bd_rate_percent=value, baseline_monotone_rungs=int(len(monotone_points(bp)[0])),
                      candidate_monotone_rungs=int(len(monotone_points(cp)[0])),
                      quality_overlap_mos=[lo, hi])
        per_clip.append(record)

    valid = [r for r in per_clip if "bd_rate_percent" in r]
    all_rates_b = [p[0] for r in per_clip for p in [(x["payload_bitrate_kbps"], x["ffmpeg_mos"])
                                                      for x in r["baseline_points"]]]
    all_rates_c = [p[0] for r in per_clip for p in [(x["payload_bitrate_kbps"], x["ffmpeg_mos"])
                                                      for x in r["candidate_points"]]]
    all_quality_b = [x["ffmpeg_mos"] for r in per_clip for x in r["baseline_points"]]
    all_quality_c = [x["ffmpeg_mos"] for r in per_clip for x in r["candidate_points"]]
    overlap_lows = [r["quality_overlap_mos"][0] for r in valid]
    overlap_highs = [r["quality_overlap_mos"][1] for r in valid]
    return {"baseline_profile": baseline_profile, "candidate_profile": candidate_profile,
            "input_sample_rate": keys[0][1] if keys else None,
            "nominal_bitrates_kbps": list(bitrates), "counts": counts,
            "bd_rate_percent": summarize([r["bd_rate_percent"] for r in valid]),
            "achieved_payload_rate_kbps": {"baseline_min": min(all_rates_b) if all_rates_b else None,
                                           "baseline_max": max(all_rates_b) if all_rates_b else None,
                                           "baseline_mean": statistics.fmean(all_rates_b) if all_rates_b else None,
                                           "candidate_min": min(all_rates_c) if all_rates_c else None,
                                           "candidate_max": max(all_rates_c) if all_rates_c else None,
                                           "candidate_mean": statistics.fmean(all_rates_c) if all_rates_c else None},
            "quality_mos_ranges": {"baseline": [min(all_quality_b), max(all_quality_b)] if all_quality_b else None,
                                   "candidate": [min(all_quality_c), max(all_quality_c)] if all_quality_c else None},
            "shared_quality_mos_range": [min(overlap_lows), max(overlap_highs)] if valid else None,
            "per_clip": per_clip}


def main():
    default_bench = Path(__file__).resolve().parents[2] / "faac-benchmark"
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--candidate", nargs="+", required=True, help="candidate results.json manifest(s)")
    p.add_argument("--baseline", nargs="+", required=True, help="baseline results.json manifest(s)")
    p.add_argument("--candidate-label")
    p.add_argument("--baseline-label", required=True)
    p.add_argument("--baseline-profile", required=True, choices=["he-aac-v1", "he-aac-v2"])
    p.add_argument("--candidate-profile", default="he-aac-v2", choices=["he-aac-v1", "he-aac-v2"])
    p.add_argument("--bitrates", default="8,12,16")
    p.add_argument("--benchmark-dir", default=str(default_bench))
    p.add_argument("--output", required=True)
    a = p.parse_args()
    bench = Path(a.benchmark_dir).resolve()
    sys.path.insert(0, str(bench / "scripts"))
    from bd_rate import IMPLAUSIBLE_PCT, MIN_RUNGS, _monotone_points, bd_rate_curve

    b_paths = [Path(x).resolve() for x in a.baseline]
    c_paths = [Path(x).resolve() for x in a.candidate]
    b_rows = dedupe([row for path in b_paths for row in load(path)
                     if row.get("encoder") == a.baseline_label])
    c_raw = [row for path in c_paths for row in load(path)]
    labels = sorted({r.get("encoder") for r in c_raw if r.get("encoder")})
    candidate_label = a.candidate_label
    if candidate_label is None:
        if len(labels) != 1:
            p.error(f"candidate manifests have labels {labels}; specify --candidate-label")
        candidate_label = labels[0]
    c_rows = dedupe([row for row in c_raw if row.get("encoder") == candidate_label])
    try:
        bitrates = sorted({int(x) for x in a.bitrates.split(",")})
    except ValueError:
        p.error("--bitrates must be a comma-separated list of integers")
    if len(bitrates) < MIN_RUNGS or any(x <= 0 for x in bitrates):
        p.error(f"at least {MIN_RUNGS} positive bitrates are required")

    def group(rows, profile):
        out = {}
        for row in rows:
            if row.get("profile") == profile:
                key = clip_key(row)
                if key[1] is not None:
                    out.setdefault(key, []).append(row)
        return out

    base_groups = group(b_rows, a.baseline_profile)
    candidate_groups = group(c_rows, a.candidate_profile)
    rates = sorted({key[1] for key in candidate_groups})
    reports = []
    for rate in rates:
        keys = sorted({k for k in candidate_groups if k[1] == rate}, key=lambda x: x[0])
        reports.append(cohort_report(base_groups, candidate_groups, keys, a.baseline_profile,
                                     a.candidate_profile, bitrates, bd_rate_curve,
                                     _monotone_points, IMPLAUSIBLE_PCT))
    provenance = {"baseline_manifests": [{"path": str(x), "sha256": sha256(x)} for x in b_paths],
                  "baseline_label": a.baseline_label, "baseline_profile": a.baseline_profile,
                  "candidate_manifests": [{"path": str(x), "sha256": sha256(x)} for x in c_paths],
                  "candidate_label": candidate_label, "candidate_profile": a.candidate_profile,
                  "metric": "ffmpeg_mos", "rate_metric": "aac_payload_bitrate_kbps",
                  "deduplication_key": ["binary_sha256", "input_path", "profile", "bitrate_kbps_total", "environment"]}
    result = {"provenance": provenance,
              "method": {"name": "scripts/bd_rate.py::bd_rate_curve", "interpolator": "monotone PCHIP",
                         "min_raw_points": MIN_RUNGS, "extrapolation": False,
                         "implausible_abs_percent_limit": IMPLAUSIBLE_PCT,
                         "bootstrap_resamples": 10000, "bootstrap_seed": 0},
              "input_counts": {"baseline_rows": len(b_rows), "candidate_rows": len(c_rows),
                               "baseline_profile_clip_sample_groups": len(base_groups),
                               "candidate_profile_clip_sample_groups": len(candidate_groups)},
              "cohorts": reports}
    Path(a.output).write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    for r in reports:
        s = r["bd_rate_percent"]
        print(f"{r['input_sample_rate']}Hz n={s['n']} mean={s['mean']} median={s['median']} "
              f"CI95={s['ci95']} counts={r['counts']}")


if __name__ == "__main__":
    main()
