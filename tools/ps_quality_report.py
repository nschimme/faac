#!/usr/bin/env python3
"""Compare PS quality harness result manifests with paired clip statistics."""
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
        raise ValueError(f"{path}: expected results mapping/list")
    return rows


def sample_rate(row):
    audio = row.get("input_audio") or {}
    if audio.get("sample_rate"):
        return int(audio["sample_rate"])
    clip = row.get("clip") or row.get("input_path")
    if not clip or not Path(clip).is_file():
        return None
    cmd = ["ffprobe", "-v", "error", "-select_streams", "a:0", "-show_entries",
           "stream=sample_rate", "-of", "default=noprint_wrappers=1:nokey=1", clip]
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode:
        return None
    try:
        return int(p.stdout.strip())
    except ValueError:
        return None


def clip_key(row):
    path = row.get("clip") or row.get("input_path") or ""
    return Path(path).name, sample_rate(row)


def percentile(values, p):
    if not values:
        return None
    ordered = sorted(float(x) for x in values)
    at = (len(ordered) - 1) * p
    lo, hi = math.floor(at), math.ceil(at)
    return ordered[lo] + (ordered[hi] - ordered[lo]) * (at - lo)


def stats(values):
    values = [float(x) for x in values if x is not None]
    if not values:
        return {"n": 0, "min": None, "p10": None, "mean": None, "ci95": None}
    rng = random.Random(0)
    n = len(values)
    boot = [statistics.fmean(rng.choice(values) for _ in range(n)) for _ in range(10000)]
    return {"n": n, "min": min(values), "p10": percentile(values, .10),
            "mean": statistics.fmean(values), "ci95": [percentile(boot, .025), percentile(boot, .975)]}


def mean(values):
    values = [float(x) for x in values if x is not None]
    return statistics.fmean(values) if values else None


def select(rows, label):
    all_label = [r for r in rows if r.get("encoder") == label]
    ok = [r for r in all_label if r.get("status") == "ok" and r.get("ffmpeg_mos") is not None]
    return all_label, ok


def grouped(rows):
    result = {}
    for row in rows:
        key = clip_key(row)
        if key[1] is not None:
            result.setdefault(key, []).append(row)
    return result


def lookup(group, profile, bitrate):
    rows = [r for r in group if r.get("profile") == profile and
            r.get("bitrate_kbps_total") == bitrate and r.get("status") == "ok"]
    return rows[0] if rows else None


def interpolate(group, profile, rate):
    ladder = sorted((float(r["payload_bitrate_kbps"]), r) for r in group
                    if r.get("profile") == profile and r.get("status") == "ok" and
                    r.get("payload_bitrate_kbps") is not None and r.get("ffmpeg_mos") is not None)
    if not ladder or rate is None:
        return None
    for achieved, row in ladder:
        if abs(rate - achieved) <= 1e-6:
            return {"mos": float(row["ffmpeg_mos"]), "coherence_error": row.get("coherence_error"),
                    "payload_bitrate_kbps": achieved, "method": "exact"}
    for (lo_rate, lo), (hi_rate, hi) in zip(ladder, ladder[1:]):
        if lo_rate < rate < hi_rate and hi_rate > lo_rate:
            t = (rate - lo_rate) / (hi_rate - lo_rate)
            return {"mos": float(lo["ffmpeg_mos"]) + t * (float(hi["ffmpeg_mos"]) - float(lo["ffmpeg_mos"])),
                    "coherence_error": (float(lo["coherence_error"]) + t * (float(hi["coherence_error"]) - float(lo["coherence_error"])))
                    if lo.get("coherence_error") is not None and hi.get("coherence_error") is not None else None,
                    "payload_bitrate_kbps": rate, "method": "linear"}
    return None


def comparison(candidate_rows, baseline_groups, target_profile, baseline_profile, bitrate, input_rate):
    pairs, misses, equal_rate = [], [], []
    for cand in candidate_rows:
        if (cand.get("profile") != target_profile or cand.get("bitrate_kbps_total") != bitrate or
                sample_rate(cand) != input_rate):
            continue
        key = clip_key(cand)
        base_group = baseline_groups.get(key, [])
        if cand.get("status") != "ok":
            misses.append({"clip": key[0], "sample_rate": key[1], "side": "candidate_error",
                           "error": cand.get("error")})
            continue
        base = lookup(base_group, baseline_profile, bitrate)
        if base is None:
            misses.append({"clip": key[0], "sample_rate": key[1], "side": "baseline_missing",
                           "profile": baseline_profile, "bitrate_kbps_total": bitrate})
            continue
        mos_delta = float(cand["ffmpeg_mos"]) - float(base["ffmpeg_mos"])
        coh_delta = (float(cand["coherence_error"]) - float(base["coherence_error"])
                     if cand.get("coherence_error") is not None and base.get("coherence_error") is not None else None)
        pair = {"clip": key[0], "sample_rate": key[1], "candidate_payload_bitrate_kbps": cand.get("payload_bitrate_kbps"),
                "baseline_payload_bitrate_kbps": base.get("payload_bitrate_kbps"),
                "candidate_ffmpeg_mos": cand.get("ffmpeg_mos"), "baseline_ffmpeg_mos": base.get("ffmpeg_mos"),
                "ffmpeg_mos_delta": mos_delta, "coherence_error_delta": coh_delta,
                "candidate_fdk_mos": cand.get("fdk_mos"), "baseline_fdk_mos": base.get("fdk_mos"),
                "fdk_mos_delta": (float(cand["fdk_mos"]) - float(base["fdk_mos"])
                                  if cand.get("fdk_mos") is not None and base.get("fdk_mos") is not None else None),
                "fdk_coherence_error_delta": (float(cand["fdk_coherence_error"]) - float(base["fdk_coherence_error"])
                                               if cand.get("fdk_coherence_error") is not None and
                                               base.get("fdk_coherence_error") is not None else None)}
        pairs.append(pair)
        achieved = cand.get("payload_bitrate_kbps")
        ref = interpolate(base_group, baseline_profile, float(achieved) if achieved is not None else None)
        if ref:
            equal_rate.append({"clip": key[0], "sample_rate": key[1], "candidate_payload_bitrate_kbps": achieved,
                               "baseline_interpolated_payload_bitrate_kbps": ref["payload_bitrate_kbps"],
                               "baseline_interpolation": ref["method"],
                               "ffmpeg_mos_delta": float(cand["ffmpeg_mos"]) - ref["mos"],
                               "coherence_error_delta": (float(cand["coherence_error"]) - float(ref["coherence_error"])
                                                         if cand.get("coherence_error") is not None and ref.get("coherence_error") is not None else None)})
    deltas = [x["ffmpeg_mos_delta"] for x in pairs]
    wins = sum(x > .02 for x in deltas)
    losses = sum(x < -.02 for x in deltas)
    largest = [x for x in sorted(pairs, key=lambda x: x["ffmpeg_mos_delta"])
               if x["ffmpeg_mos_delta"] < 0][:10]
    coherence_stats = stats([x["coherence_error_delta"] for x in pairs])
    equal_coherence_stats = stats([x["coherence_error_delta"] for x in equal_rate])
    return {"target_profile": target_profile, "baseline_profile": baseline_profile,
            "input_sample_rate": input_rate,
            "nominal_bitrate_kbps": bitrate, "matched_pairs": len(pairs), "missing": misses,
            "ffmpeg_mos_delta": stats(deltas),
            "coherence_error_delta": coherence_stats,
            "coherence_error_delta_mean": coherence_stats["mean"],
            "fdk_coherence_error_delta_mean": mean([x["fdk_coherence_error_delta"] for x in pairs]),
            "candidate_payload_bitrate_kbps_mean": mean([x["candidate_payload_bitrate_kbps"] for x in pairs]),
            "baseline_payload_bitrate_kbps_mean": mean([x["baseline_payload_bitrate_kbps"] for x in pairs]),
            "fdk_mos_delta": stats([x["fdk_mos_delta"] for x in pairs]),
            "wins_over_0_02": wins, "losses_below_minus_0_02": losses,
            "largest_losses": largest, "paired_results": pairs,
            "equal_achieved_payload": {"n": len(equal_rate), "candidate_count": len(pairs),
                                       "coverage": len(equal_rate) / len(pairs) if pairs else 0.0,
                                       "ffmpeg_mos_delta": stats([x["ffmpeg_mos_delta"] for x in equal_rate]),
                                       "coherence_error_delta": equal_coherence_stats,
                                       "coherence_error_delta_mean": equal_coherence_stats["mean"],
                                       "pairs": equal_rate}}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--baseline", required=True)
    p.add_argument("--candidate", required=True)
    p.add_argument("--baseline-label")
    p.add_argument("--candidate-label")
    p.add_argument("--reference-profile", choices=["he-aac-v1", "he-aac-v2"])
    p.add_argument("--output")
    a = p.parse_args()
    baseline_path, candidate_path = Path(a.baseline).resolve(), Path(a.candidate).resolve()
    baseline, candidate = load(baseline_path), load(candidate_path)
    b_labels = sorted({r.get("encoder") for r in baseline if r.get("encoder")})
    c_labels = sorted({r.get("encoder") for r in candidate if r.get("encoder")})
    if not a.baseline_label:
        if len(b_labels) != 1:
            p.error(f"baseline has labels {b_labels}; specify --baseline-label")
        a.baseline_label = b_labels[0]
    if not a.candidate_label:
        if len(c_labels) != 1:
            p.error(f"candidate has labels {c_labels}; specify --candidate-label")
        a.candidate_label = c_labels[0]
    b_all, b_ok = select(baseline, a.baseline_label)
    c_all, c_ok = select(candidate, a.candidate_label)
    b_profiles = sorted({r.get("profile") for r in b_all if r.get("profile")})
    target_profiles = sorted({r.get("profile") for r in c_all if r.get("profile")})
    bitrates = sorted({r.get("bitrate_kbps_total") for r in c_all if r.get("bitrate_kbps_total") is not None})
    b_groups, c_groups = grouped(b_ok), grouped(c_ok)
    reports = []
    for profile in target_profiles:
        baseline_profiles = [profile]
        if a.reference_profile and a.reference_profile != profile:
            baseline_profiles.append(a.reference_profile)
        for base_profile in baseline_profiles:
            for bitrate in bitrates:
                sample_rates = {sample_rate(r) for r in c_all
                                if r.get("profile") == profile and r.get("bitrate_kbps_total") == bitrate}
                for input_rate in sorted(sample_rates, key=lambda value: -1 if value is None else value):
                    reports.append(comparison(c_all, b_groups, profile, base_profile, bitrate, input_rate))
    result = {"provenance": {"baseline_manifest": str(baseline_path), "baseline_sha256": sha256(baseline_path),
                             "baseline_label": a.baseline_label, "candidate_manifest": str(candidate_path),
                             "candidate_sha256": sha256(candidate_path), "candidate_label": a.candidate_label},
              "configuration": {"reference_profile": a.reference_profile, "candidate_profiles": target_profiles,
                                "candidate_nominal_bitrates_kbps": bitrates, "comparison_profiles": b_profiles,
                                "bootstrap_resamples": 10000, "bootstrap_seed": 0,
                                "win_loss_threshold": .02,
                                "equal_bitrate_policy": "linear interpolation inside baseline achieved-payload range; exact points only for one-point ladders; no extrapolation"},
              "input_counts": {"baseline": {"selected": len(b_all), "ok": len(b_ok), "errors": len(b_all)-len(b_ok),
                                              "error_rows": [{"clip": r.get("clip"), "profile": r.get("profile"),
                                                              "bitrate_kbps_total": r.get("bitrate_kbps_total"),
                                                              "error": r.get("error")} for r in b_all if r not in b_ok]},
                               "candidate": {"selected": len(c_all), "ok": len(c_ok), "errors": len(c_all)-len(c_ok),
                                             "error_rows": [{"clip": r.get("clip"), "profile": r.get("profile"),
                                                             "bitrate_kbps_total": r.get("bitrate_kbps_total"),
                                                             "error": r.get("error")} for r in c_all if r not in c_ok]}},
              "comparisons": reports}
    rendered = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if a.output:
        Path(a.output).write_text(rendered)
    else:
        sys.stdout.write(rendered)
    for r in reports:
        m = r["ffmpeg_mos_delta"]
        print(f"{r['target_profile']} vs {r['baseline_profile']} {r['nominal_bitrate_kbps']}k "
              f"{r['input_sample_rate']}Hz: "
              f"n={r['matched_pairs']} dMOS={m['mean'] if m['mean'] is not None else 'n/a'} "
              f"CI95={m['ci95']} wins/losses={r['wins_over_0_02']}/{r['losses_below_minus_0_02']} "
              f"equal-rate={r['equal_achieved_payload']['n']}/{r['matched_pairs']}", file=sys.stderr)


if __name__ == "__main__":
    main()
