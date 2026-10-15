#!/usr/bin/env python3
"""Verify a separate candidate checkout against the selected HE-AAC v1 stream."""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import math
import os
import statistics
import subprocess
import sys
import tempfile
import threading
import wave
from datetime import datetime, timezone
from pathlib import Path

BENCH = Path("/Users/nschimme/gitprojects/faac-benchmark")
PROBE = Path("/private/tmp/faac-he-v1-probes")
CANDIDATE = Path("/private/tmp/faac-he-v1-candidate")
DEFAULT_RUN = PROBE / "probe/results-all"
DEFAULT_OUT_ROOT = PROBE / "probe"
VARIANTS = {
    "limiter_gain_3": {
        "candidate_root": Path("/private/tmp/faac-he-v1-candidate"),
        "environment": {"FAAC_SBR_PROBE_LIMITER_GAIN": "3"},
    },
    "noise_18": {
        "candidate_root": Path("/private/tmp/faac-he-v1-noise"),
        "environment": {"FAAC_SBR_PROBE_NOISE": "18"},
    },
}
PROBE_PREFIX = "FAAC_SBR_PROBE_"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--run-dir", type=Path, default=DEFAULT_RUN,
                    help="full-corpus run_features output directory")
    ap.add_argument("--output-dir", type=Path,
                    help="output directory (default: probe/candidate-equivalence-<variant>)")
    ap.add_argument("--candidate-bin", type=Path,
                    help="candidate frontend binary; default is selected variant's build-candidate binary")
    ap.add_argument("--variant", choices=tuple(VARIANTS), default="limiter_gain_3")
    ap.add_argument("--baseline-at-rates32", default="",
                    help="comma-separated rates whose expected stream is baseline_default")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--timing", action="store_true",
                    help="after equivalence workers finish, run the 12-pair timing probe")
    ap.add_argument("--timing-only", action="store_true",
                    help="run only timing; use after equivalence workers have stopped")
    args = ap.parse_args()
    if args.jobs <= 0:
        ap.error("--jobs must be a positive integer")

    variant = args.variant
    variant_env = VARIANTS[variant]["environment"]
    candidate_bin = (args.candidate_bin.expanduser().resolve() if args.candidate_bin
                     else VARIANTS[variant]["candidate_root"] / "build-candidate/frontend/faac")
    candidate_root = candidate_bin.parents[2]
    out_dir = (args.output_dir.expanduser().resolve() if args.output_dir
               else DEFAULT_OUT_ROOT / f"candidate-equivalence-{variant}")
    try:
        out_dir.relative_to(DEFAULT_OUT_ROOT.resolve())
    except ValueError:
        raise RuntimeError(f"Keep candidate validation output under {DEFAULT_OUT_ROOT}: {out_dir}")
    out_dir.mkdir(parents=True, exist_ok=True)
    result_path = out_dir / "candidate-equivalence.json"

    os.environ.setdefault("NUMBA_DISABLE_JIT", "1")
    sys.path.insert(0, str(BENCH))
    sys.path.insert(0, str(PROBE / "probe"))
    from codec_bench.encoders import FAACEncoder
    from codec_bench.decoders import FDKDecoder
    import utils
    from compare_results import bootstrap_mean_ci
    sys.path.insert(0, str(BENCH / "scripts"))
    from bd_rate import bd_rate_curve
    from run_features import (BASE_BIN, CAND_BIN as PROBE_BIN, FDK_BIN, atomic_json,
                              decode_and_convert, git_head, linked_file_manifest,
                              media_duration, payload_bitrate_kbps, payload_sha256,
                              sha256, source_diff_hash, source_tree_hash, stream_profile)

    if not args.timing_only:
        results_path = args.run_dir / "feature-results.json"
        results = json.loads(results_path.read_text())
        clips = sorted({row["clip"] for row in results["rows"].values()
                        if row.get("variant") == variant})
        rates = sorted({int(row["target_kbps"]) for row in results["rows"].values()
                        if row.get("variant") == variant})
        selected_rows = [row for row in results["rows"].values() if row.get("variant") == variant]
        if len(selected_rows) != len(clips) * len(rates) or not clips or not rates:
            raise RuntimeError(f"Selected feature matrix is incomplete: {len(selected_rows)} rows")
        if len(clips) != 49 or len(rates) != 4 or len(selected_rows) != 196:
            raise RuntimeError(f"Expected full corpus 49 clips x 4 rates = 196 rows; got {len(clips)} x {len(rates)} = {len(selected_rows)}")
        for binary in (candidate_bin, PROBE_BIN, FDK_BIN):
            if not binary.is_file():
                raise FileNotFoundError(binary)

        baseline_rates = {int(x) for x in args.baseline_at_rates32.split(",") if x.strip()}
        unknown_baseline_rates = baseline_rates - set(rates)
        if unknown_baseline_rates:
            ap.error(f"--baseline-at-rates32 contains rates not in the run: {sorted(unknown_baseline_rates)}")
        if variant == "noise_18" and baseline_rates:
            ap.error("--baseline-at-rates32 is intended for limiter_gain_3 comparisons")
        candidate_enc_dir = out_dir / "encodes"
        candidate_wav_dir = out_dir / "decoded-wav"
        candidate_enc_dir.mkdir(parents=True, exist_ok=True)
        candidate_wav_dir.mkdir(parents=True, exist_ok=True)
        source_dir = BENCH / "data/external/audio"
        decoder = FDKDecoder("FDK AAC", str(FDK_BIN))

        manifest = {
            "schema": 1,
            "created_utc": datetime.now(timezone.utc).isoformat(),
            "feature": variant,
            "feature_environment": variant_env,
            "baseline_at_rates32": sorted(baseline_rates),
            "comparison": "ordered ffprobe AAC packet hashes and FDK-decoded 48 kHz stereo WAV SHA-256",
            "mos": "not rerun; inherited from the expected tested row only when packet and decoded PCM identities both match",
            "run_dir": str(args.run_dir.resolve()),
            "run_results_sha256": sha256(results_path),
            "clips": clips,
            "rates_kbps": rates,
            "expected_rows": len(selected_rows),
            "probe": {
                "root": str(PROBE), "commit": git_head(PROBE),
                "source_tree_sha256": source_tree_hash(PROBE),
                "source_diff_sha256": source_diff_hash(PROBE),
                "binary": str(PROBE_BIN), "binary_sha256": sha256(PROBE_BIN),
                "linked_libraries": linked_file_manifest(PROBE_BIN),
            },
            "candidate": {
                "root": str(candidate_root), "commit": git_head(candidate_root),
                "source_tree_sha256": source_tree_hash(candidate_root),
                "source_diff_sha256": source_diff_hash(candidate_root),
                "binary": str(candidate_bin), "binary_sha256": sha256(candidate_bin),
                "linked_libraries": linked_file_manifest(candidate_bin),
                "libfaac": None,
            },
            "fdk": {"binary": str(FDK_BIN), "binary_sha256": sha256(FDK_BIN),
                    "linked_libraries": linked_file_manifest(FDK_BIN)},
            "rows": {},
        }
        candidate_lib = candidate_root / "build-candidate/libfaac/libfaac.2.dylib"
        if candidate_lib.is_file():
            manifest["candidate"]["libfaac"] = {"path": str(candidate_lib), "sha256": sha256(candidate_lib),
                                                 "size_bytes": candidate_lib.stat().st_size}
        result_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
        results_lock = threading.Lock()

        def validate_row(source_row):
            clip, rate = source_row["clip"], int(source_row["target_kbps"])
            src = source_dir / clip
            with wave.open(str(src), "rb") as w:
                if w.getframerate() != 48000 or w.getnchannels() != 2:
                    raise RuntimeError(f"Source is not 48 kHz stereo: {src}")
            source_feature_key = f"{variant}|{clip}|{rate}"
            comparison_variant = "baseline_default" if rate in baseline_rates else variant
            comparison_row = results["rows"].get(f"{comparison_variant}|{clip}|{rate}")
            if comparison_row is None:
                raise KeyError(f"Missing expected comparison row: {comparison_variant}|{clip}|{rate}")
            source_encoded = Path(comparison_row["encoded_path"])
            if not source_encoded.is_file():
                raise FileNotFoundError(source_encoded)
            key = source_feature_key
            enc = candidate_enc_dir / str(rate) / (Path(clip).stem + ".m4a")
            decoded = candidate_wav_dir / str(rate) / (Path(clip).stem + ".wav")
            enc.parent.mkdir(parents=True, exist_ok=True)
            decoded.parent.mkdir(parents=True, exist_ok=True)

            encoder = FAACEncoder("faac-candidate", str(candidate_bin), profile="he")
            cmd = encoder.get_encode_cmd(str(src), str(enc), rate, 2, 48000)
            if "--object-type" not in cmd or cmd[cmd.index("--object-type") + 1] != "he-aac-v1":
                raise RuntimeError(f"Candidate command does not force HE-AAC v1: {cmd}")
            env = dict(os.environ)
            for env_key in list(env):
                if env_key.startswith(PROBE_PREFIX):
                    env.pop(env_key)
            subprocess.run(cmd, check=True, env=env, capture_output=True, text=True)
            profile = stream_profile(enc)
            if profile != "HE-AAC":
                raise RuntimeError(f"Candidate profile is {profile!r} for {clip}/{rate}")
            packet_hash = payload_sha256(enc)
            source_packet_hash = comparison_row.get("payload_sha256") or payload_sha256(source_encoded)

            decoded.unlink(missing_ok=True)
            with tempfile.TemporaryDirectory(prefix="candidate-fdk-") as td:
                decode_and_convert(enc, decoded, decoder, utils, Path(td))
            decoded_hash = sha256(decoded)
            source_wav = Path(comparison_row["decoded_path"])
            source_decoded_hash = comparison_row.get("decoded_sha256") or sha256(source_wav)
            packet_equal = packet_hash == source_packet_hash
            pcm_equal = decoded_hash == source_decoded_hash
            actual_payload = (float(comparison_row["actual_payload_kbps"])
                              if packet_equal else payload_bitrate_kbps(enc, media_duration(src)))
            row = {
                "key": key, "clip": clip, "target_kbps": rate,
                "comparison_variant": comparison_variant,
                "profile": profile,
                "source_packet_sha256": source_packet_hash,
                "candidate_packet_sha256": packet_hash,
                "packet_identity_equal": packet_equal,
                "source_decoded_sha256": source_decoded_hash,
                "candidate_decoded_sha256": decoded_hash,
                "decoded_pcm_equal": pcm_equal,
                "inherited_mos": comparison_row["mos"] if packet_equal and pcm_equal else None,
                "actual_payload_kbps": actual_payload,
                "mos_source": "inherited_matching_tested_stream" if packet_equal and pcm_equal else None,
                "candidate_encoded_path": str(enc),
                "candidate_encoded_sha256": sha256(enc),
                "candidate_decoded_path": str(decoded),
            }
            with results_lock:
                manifest["rows"][key] = row
                manifest["completed_rows"] = len(manifest["rows"])
                atomic_json(result_path, manifest)
            print(f"validated {key}: packet={'same' if packet_equal else 'DIFF'}, PCM={'same' if pcm_equal else 'DIFF'}", flush=True)

        with ThreadPoolExecutor(max_workers=args.jobs) as executor:
            futures = [executor.submit(validate_row, row) for row in selected_rows]
            for future in as_completed(futures):
                future.result()
        matched = sum(1 for row in manifest["rows"].values()
                      if row["packet_identity_equal"] and row["decoded_pcm_equal"])
        manifest["equivalence"] = {
            "matched_rows": matched, "total_rows": len(manifest["rows"]),
            "all_match": matched == 196,
            "mos_reused_rows": sum(1 for row in manifest["rows"].values() if row["mos_source"]),
            "size_tested": False, "speed_tested": False,
        }
        atomic_json(result_path, manifest)
        print(f"Equivalence: {matched}/196 packet+PCM identities match. Results: {result_path}", flush=True)

        # Derive the candidate's quality record only from matching measured
        # streams. Any unmatched candidate output has unknown MOS and is omitted.
        per_rate = {}
        for rate in rates:
            pairs = []
            for clip in clips:
                candidate_row = manifest["rows"][f"{variant}|{clip}|{rate}"]
                baseline_row = results["rows"][f"baseline_default|{clip}|{rate}"]
                if candidate_row["inherited_mos"] is not None:
                    pairs.append({
                        "clip": clip, "candidate_mos": candidate_row["inherited_mos"],
                        "baseline_mos": baseline_row["mos"],
                        "mos_delta": candidate_row["inherited_mos"] - baseline_row["mos"],
                        "candidate_payload_kbps": candidate_row["actual_payload_kbps"],
                        "baseline_payload_kbps": baseline_row["actual_payload_kbps"],
                    })
            mos_ci = bootstrap_mean_ci([p["mos_delta"] for p in pairs])
            rate_delta = [p["candidate_payload_kbps"] - p["baseline_payload_kbps"] for p in pairs]
            rate_pct = [100.0 * (p["candidate_payload_kbps"] / p["baseline_payload_kbps"] - 1.0)
                        for p in pairs]
            per_rate[str(rate)] = {
                "matched_clips": len(pairs), "pairs": pairs,
                "mean_candidate_mos": statistics.mean(p["candidate_mos"] for p in pairs) if pairs else None,
                "mean_baseline_mos": statistics.mean(p["baseline_mos"] for p in pairs) if pairs else None,
                "mean_mos_delta": statistics.mean(p["mos_delta"] for p in pairs) if pairs else None,
                "mos_delta_bootstrap_95_ci": {"lo": mos_ci["lo"], "hi": mos_ci["hi"]} if mos_ci else None,
                "mean_actual_rate_delta_kbps": statistics.mean(rate_delta) if pairs else None,
                "mean_actual_rate_delta_percent": statistics.mean(rate_pct) if pairs else None,
            }
        per_clip_bd, skipped_bd = [], []
        for clip in clips:
            cand_points, base_points = [], []
            for rate in rates:
                candidate_row = manifest["rows"][f"{variant}|{clip}|{rate}"]
                baseline_row = results["rows"][f"baseline_default|{clip}|{rate}"]
                if candidate_row["inherited_mos"] is None:
                    cand_points = []
                    break
                cand_points.append((candidate_row["actual_payload_kbps"], candidate_row["inherited_mos"]))
                base_points.append((baseline_row["actual_payload_kbps"], baseline_row["mos"]))
            value = bd_rate_curve(base_points, cand_points) if cand_points else None
            if value is None:
                skipped_bd.append(clip)
            else:
                per_clip_bd.append({"clip": clip, "bd_rate_percent": value})
        bd_values = [r["bd_rate_percent"] for r in per_clip_bd]
        bd_ci = bootstrap_mean_ci(bd_values)
        quality = {
            "schema": 1, "variant": variant,
            "baseline_at_rates32": sorted(baseline_rates),
            "source": "measured runner rows inherited only after packet and PCM equality",
            "bd_rate_method": "faac-benchmark/scripts/bd_rate.py:bd_rate_curve",
            "per_rate": per_rate, "per_clip_bd_rate": per_clip_bd,
            "skipped_bd_clips": skipped_bd,
            "mean_bd_rate_percent": statistics.mean(bd_values) if bd_values else None,
            "median_bd_rate_percent": statistics.median(bd_values) if bd_values else None,
            "mean_bd_rate_bootstrap_95_ci": {"lo": bd_ci["lo"], "hi": bd_ci["hi"]} if bd_ci else None,
            "matched_stream_rows": matched, "total_rows": 196,
        }
        quality_path = out_dir / "candidate-quality-summary.json"
        atomic_json(quality_path, quality)
        quality_md = [
            f"# Candidate quality summary: {variant}", "",
            f"Measured MOS is inherited only for packet and PCM identical rows. {matched}/196 rows matched.",
            "",
            "| Target | Matched clips | Baseline MOS | Candidate MOS | MOS delta (95% paired CI) | Actual rate delta |",
            "|---:|---:|---:|---:|---:|---:|",
        ]
        for rate in rates:
            item = per_rate[str(rate)]
            ci = item["mos_delta_bootstrap_95_ci"]
            ci_text = "n/a" if ci is None else f"[{ci['lo']:+.5f}, {ci['hi']:+.5f}]"
            rate_text = "n/a" if item["mean_actual_rate_delta_kbps"] is None else (
                f"{item['mean_actual_rate_delta_kbps']:+.3f} kbps ({item['mean_actual_rate_delta_percent']:+.3f}%)")
            baseline_text = "n/a" if item["mean_baseline_mos"] is None else f"{item['mean_baseline_mos']:.5f}"
            candidate_text = "n/a" if item["mean_candidate_mos"] is None else f"{item['mean_candidate_mos']:.5f}"
            delta_text = "n/a" if item["mean_mos_delta"] is None else f"{item['mean_mos_delta']:+.5f}"
            quality_md.append(
                f"| {rate} kbps | {item['matched_clips']} | "
                f"{baseline_text} | {candidate_text} | {delta_text} {ci_text} | {rate_text} |"
            )
        mean_bd_text = "n/a" if quality["mean_bd_rate_percent"] is None else f"{quality['mean_bd_rate_percent']:+.3f}%"
        median_bd_text = "n/a" if quality["median_bd_rate_percent"] is None else f"{quality['median_bd_rate_percent']:+.3f}%"
        quality_md.extend(["", f"Mean BD-rate: {mean_bd_text}; median: {median_bd_text}; "
                           f"mean bootstrap 95% CI: {quality['mean_bd_rate_bootstrap_95_ci'] or 'n/a'}.",
                           "", "This is actual candidate quality inherited from exact tested streams. Unmatched streams have no MOS estimate. Size and speed remain separate gates.", ""])
        (out_dir / "RESULTS.md").write_text("\n".join(quality_md))
        print(f"Candidate quality summary: {quality_path}", flush=True)

    if args.timing or args.timing_only:
        if not candidate_bin.is_file() or not BASE_BIN.is_file():
            raise FileNotFoundError("Baseline/candidate binaries are required for timing")
        if args.timing_only:
            result_path = out_dir / "candidate-equivalence.json"
            if result_path.is_file():
                manifest = json.loads(result_path.read_text())
            else:
                manifest = {"schema": 1, "feature": variant, "rows": {}}
        else:
            manifest["equivalence"]["workers_stopped_before_timing"] = True

        encoder_baseline = FAACEncoder("faac-baseline", str(BASE_BIN), profile="he")
        encoder_candidate = FAACEncoder("faac-candidate", str(candidate_bin), profile="he")
        timing_rows = []
        timing_dir = out_dir / "timing"
        timing_dir.mkdir(parents=True, exist_ok=True)
        for clip in ("sandman.16b48k.wav", "velvet.16b48k.wav"):
            src = BENCH / "data/external/audio" / clip
            if not src.is_file():
                raise FileNotFoundError(src)
            deltas = []
            for iteration in range(12):
                order = ["baseline", "candidate"] if iteration % 2 == 0 else ["candidate", "baseline"]
                measurements = {}
                for label in order:
                    encoder = encoder_baseline if label == "baseline" else encoder_candidate
                    with tempfile.TemporaryDirectory(prefix=f"he-timing-{label}-", dir=timing_dir) as td:
                        output = Path(td) / f"{clip}.{iteration}.m4a"
                        cmd = encoder.get_encode_cmd(str(src), str(output), 64, 2, 48000)
                        if "--object-type" not in cmd or cmd[cmd.index("--object-type") + 1] != "he-aac-v1":
                            raise RuntimeError(f"Timing command does not force HE-AAC v1: {cmd}")
                        env = dict(os.environ)
                        for env_key in list(env):
                            if env_key.startswith(PROBE_PREFIX):
                                env.pop(env_key)
                        proc, duration, rss_kb = utils.measure_peak_ram(cmd, env=env, check=True, timeout=120)
                        if proc.returncode != 0 or not output.is_file():
                            raise RuntimeError(f"Timing encode failed: {label} {clip} pair {iteration}")
                        measurements[label] = {"duration_seconds": duration, "peak_rss_kb": rss_kb,
                                               "output_size_bytes": output.stat().st_size,
                                               "order_index": order.index(label)}
                delta = 100.0 * (measurements["candidate"]["duration_seconds"] /
                                 measurements["baseline"]["duration_seconds"] - 1.0)
                rss_delta = 100.0 * (measurements["candidate"]["peak_rss_kb"] /
                                      measurements["baseline"]["peak_rss_kb"] - 1.0)
                deltas.append(delta)
                timing_rows.append({"clip": clip, "iteration": iteration + 1,
                                    "execution_order": order, "measurements": measurements,
                                    "candidate_vs_baseline_duration_delta_percent": delta,
                                    "candidate_vs_baseline_rss_delta_percent": rss_delta})
                print(f"timing {clip} pair {iteration + 1}/12: "
                      f"duration Δ={delta:+.2f}%, RSS Δ={rss_delta:+.2f}%", flush=True)

        all_deltas = [row["candidate_vs_baseline_duration_delta_percent"] for row in timing_rows]
        ci = bootstrap_mean_ci(all_deltas)
        timing_result = {
            "method": "12 alternating paired encodes per clip; two clips; measure_peak_ram",
            "comparison": "candidate encoder vs baseline encoder; all probe variables unset",
            "timing_rows": timing_rows,
            "pair_count": len(timing_rows),
            "paired_median_duration_delta_percent": statistics.median(all_deltas) if all_deltas else None,
            "paired_mean_duration_delta_percent": sum(all_deltas) / len(all_deltas) if all_deltas else None,
            "mean_delta_bootstrap_95_ci_percent": {"lo": ci["lo"], "hi": ci["hi"]} if ci else None,
            "paired_median_rss_delta_percent": statistics.median(
                row["candidate_vs_baseline_rss_delta_percent"] for row in timing_rows) if timing_rows else None,
            "interpretation": "Measurement only; no speed-win claim. Repeat under the established benchmark gate before drawing a performance conclusion.",
        }
        manifest["timing"] = timing_result
        atomic_json(timing_dir / "candidate-timing.json", timing_result)
        atomic_json(out_dir / "candidate-equivalence.json", manifest)
        print(f"Timing measurements saved in {timing_dir / 'candidate-timing.json'}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
