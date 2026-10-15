#!/usr/bin/env python3
"""Run the frozen transient-border mode1 full-corpus follow-up.

The harness is separate from the frozen envelope-count runner. It evaluates
modes 1/2 at two lambdas in baseline and retained-stack contexts. Running the
matrix requires explicit --start after source review and root approval.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import shutil
import statistics
import subprocess
import sys
import tempfile
import threading
import wave
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime, timezone
from pathlib import Path

ARCHIVE = Path("/Users/nschimme/gitprojects/faac/.cache/he-v1-feature-study/transient-border")
PROBE = Path("/private/tmp/faac-he-v1-transient-border")
PROBE_BIN = PROBE / "build-probe/frontend/faac"
PROBE_LIB = PROBE / "build-probe/libfaac/libfaac.2.dylib"
PROBE_PATCH = PROBE / "probe/transient-border.patch"
BASE_BIN = Path("/private/tmp/faac-he-v1-probes/build-baseline/frontend/faac")
BASE_LIB = Path("/private/tmp/faac-he-v1-probes/build-baseline/libfaac/libfaac.2.dylib")
STACK_BIN = Path("/private/tmp/faac-he-v1-candidate/build-candidate/frontend/faac")
STACK_LIB = Path("/private/tmp/faac-he-v1-candidate/build-candidate/libfaac/libfaac.2.dylib")
BENCH = Path("/Users/nschimme/gitprojects/faac-benchmark")
FDK_BIN = BENCH / "bin/fdkdec"
FAAD_BIN = Path("/private/tmp/faac-he-v1-decoder-build/frontend/faad")
FFMPEG = shutil.which("ffmpeg")
BASE_RESULTS = Path("/private/tmp/faac-he-v1-probes/probe/results-all/feature-results.json")
STACK_RESULTS = Path("/Users/nschimme/gitprojects/faac/.cache/he-v1-feature-study/stack/results-all/stack-results.json")
BASE_KEY = "baseline_default"
STACK_KEY = "stack18"
CLIPS = tuple(sorted(path.name for path in (BENCH / "data/external/audio").glob("*.wav")))
RATES = (32, 48, 64, 96)
LAMBDAS = ("0.001", "0.003")
OUTPUT = ARCHIVE / "full-mode1-final"
ALLOWLIST_ENV = ("NUMBA_DISABLE_JIT", "CCACHE_DISABLE")


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def atomic_json(path: Path, value) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")
    os.replace(tmp, path)


def envcount_module():
    tools_dir = Path(__file__).resolve().parents[2] / "envelope-count/tools"
    sys.path.insert(0, str(tools_dir))
    import run_envcount
    return run_envcount


def build_identity(ec, runner: Path) -> dict:
    clips_root = BENCH / "data/external/audio"
    return {
        "runner_sha256": sha256(runner),
        "benchmark_commit": ec.git_head(BENCH),
        "probe_head": ec.git_head(PROBE),
        "probe_diff_sha256": ec.source_diff_hash(PROBE),
        "probe_patch_path": str(PROBE_PATCH),
        "probe_patch_sha256": sha256(PROBE_PATCH),
        "probe_changed_file_sha256": {
            name: sha256(PROBE / name) for name in (
                "libfaac/sbr.c", "libfaac/sbr_analysis.c", "libfaac/sbr_analysis.h",
                "libfaac/sbr_bitstream.c", "libfaac/sbr_internal.h")
        },
        "binaries": {
            str(BASE_BIN): sha256(BASE_BIN), str(STACK_BIN): sha256(STACK_BIN),
            str(PROBE_BIN): sha256(PROBE_BIN), str(FDK_BIN): sha256(FDK_BIN),
            str(FAAD_BIN): sha256(FAAD_BIN), str(FFMPEG): sha256(Path(FFMPEG)),
        },
        "libraries": {
            str(BASE_LIB): sha256(BASE_LIB), str(STACK_LIB): sha256(STACK_LIB),
            str(PROBE_LIB): sha256(PROBE_LIB),
        },
        "reference_results": {
            str(BASE_RESULTS): sha256(BASE_RESULTS), str(STACK_RESULTS): sha256(STACK_RESULTS),
        },
        "clips": list(CLIPS),
        "clip_sha256": {clip: sha256(clips_root / clip) for clip in CLIPS},
        "rates_kbps": list(RATES),
        "matrix": {
            "reference_rows": ["baseline_ref", "stack_ref"],
            "probe_off_controls": ["off_base", "off_stack"],
            "features": feature_matrix(),
            "expected_rows": (2 + len(feature_matrix())) * len(CLIPS) * len(RATES),
        },
        "encoder_environment_recorded": "only explicit FAAC_SBR_BORDER_MODE, FAAC_SBR_BORDER_LAMBDA, FAAC_SBR_ENVCOUNT_STACK plus NUMBA_DISABLE_JIT/CCACHE_DISABLE; no full process environment is serialized",
        "scoring": "strict FDK before MOS; Zimtohrli audio mode; actual AAC packet payload bitrate; PCHIP BD-rate and four-clip bootstrap; strict FAAD3 and FFmpeg after scoring",
        "starting_scoring_requires_root_go": True,
    }


def feature_matrix() -> dict:
    out = {"off_base": {"FAAC_SBR_BORDER_MODE": "0", "FAAC_SBR_ENVCOUNT_STACK": "0"},
           "off_stack": {"FAAC_SBR_BORDER_MODE": "0", "FAAC_SBR_ENVCOUNT_STACK": "1"}}
    for stack, suffix in ((0, "base"), (1, "stack")):
        for lam in LAMBDAS:
            key = f"mode1_lam{lam.replace('.', '')}_{suffix}"
            out[key] = {"FAAC_SBR_BORDER_MODE": "1", "FAAC_SBR_BORDER_LAMBDA": lam,
                        "FAAC_SBR_ENVCOUNT_STACK": str(stack)}
    return out


def clean_encoder_env(flags: dict) -> dict:
    # PATH is needed for frontend helper tools; its value is not archived.
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "LANG": "C", "LC_ALL": "C"}
    for name in ALLOWLIST_ENV:
        if name in os.environ:
            env[name] = os.environ[name]
    env.update(flags)
    return env


def make_reference_rows(ec, base_doc: dict, stack_doc: dict, rows: dict) -> dict:
    result = {}
    for clip in CLIPS:
        for rate in RATES:
            for label, source_rows, source_key in (
                ("baseline_ref", base_doc, BASE_KEY), ("stack_ref", stack_doc, STACK_KEY)):
                old = source_rows["rows"][f"{source_key}|{clip}|{rate}"]
                encoded, decoded = Path(old["encoded_path"]), Path(old["decoded_path"])
                if not encoded.is_file() or not decoded.is_file():
                    raise FileNotFoundError(f"Missing scored reference packet/FDK PCM: {encoded} {decoded}")
                if old.get("encoded_sha256") != ec.sha256(encoded) or old.get("decoded_sha256") != ec.sha256(decoded):
                    raise RuntimeError(f"Reference file hash changed: {source_key}|{clip}|{rate}")
                packet_hash = ec.payload_identity(encoded)
                if ec.profile(encoded) != "HE-AAC":
                    raise RuntimeError(f"Reference stream is not HE-AAC: {source_key}|{clip}|{rate}")
                if packet_hash != old.get("payload_sha256"):
                    raise RuntimeError(f"Reference AAC packet identity changed: {source_key}|{clip}|{rate}")
                key = f"{label}|{clip}|{rate}"
                result[key] = {
                    "variant": label, "clip": clip, "target_kbps": rate,
                    "actual_payload_kbps": old["actual_payload_kbps"], "profile": "HE-AAC",
                    "mos": float(old["mos"]), "metric_backend": old.get("metric_backend", "zimtohrli"),
                    "mos_source": f"archived-reference:{source_key}", "payload_sha256": packet_hash,
                    "decoded_sha256": old["decoded_sha256"], "encoded_path": str(encoded),
                    "encoded_sha256": old["encoded_sha256"], "decoded_path": str(decoded),
                    "decoded_file_sha256": old["decoded_sha256"], "fdk_status": "previously strict-decoded",
                    "environment": {}, "command": old.get("command", []),
                }
    return result


def encode_decode(ec, label: str, flags: dict, reference: dict | None, clip: str, rate: int,
                  out_dir: Path, FAACEncoder, FDKDecoder, utils) -> dict:
    audio = BENCH / "data/external/audio" / clip
    encoded = out_dir / "encodes" / label / str(rate) / (Path(clip).stem + ".m4a")
    decoded = out_dir / "fdk-wav" / label / str(rate) / (Path(clip).stem + ".wav")
    encoded.parent.mkdir(parents=True, exist_ok=True)
    decoded.parent.mkdir(parents=True, exist_ok=True)
    encoder = FAACEncoder("faac-transient-border-gate", str(PROBE_BIN), profile="he")
    command = encoder.get_encode_cmd(str(audio), str(encoded), rate, 2, 48000)
    if "--object-type" not in command or command[command.index("--object-type") + 1] != "he-aac-v1":
        raise RuntimeError(f"Expected forced HE-AAC v1 command for {label}|{clip}|{rate}: {command}")
    env = clean_encoder_env(flags)
    completed = subprocess.run(command, check=True, capture_output=True, text=True, env=env)
    profile = ec.profile(encoded)
    if profile != "HE-AAC":
        raise RuntimeError(f"Expected HE-AAC profile, got {profile}: {label}|{clip}|{rate}")
    with tempfile.TemporaryDirectory(prefix="border-fdk-") as temp:
        ec.decode_fdk(encoded, decoded, FDKDecoder("FDK AAC", str(FDK_BIN)), utils, Path(temp))
    packet = ec.payload_identity(encoded)
    pcm = ec.sha256(decoded)
    actual = ec.payload_bitrate(encoded, ec.wav_duration(audio))
    row = {
        "variant": label, "clip": clip, "target_kbps": rate,
        "actual_payload_kbps": actual, "profile": profile,
        "payload_sha256": packet, "decoded_sha256": pcm,
        "encoded_path": str(encoded), "encoded_sha256": ec.sha256(encoded),
        "decoded_path": str(decoded), "decoded_file_sha256": ec.sha256(decoded),
        "fdk_status": "strict decoded to 48 kHz stereo", "command": command,
        "stderr_tail": completed.stderr[-1000:], "environment": flags,
        "mos": None, "metric_backend": None, "mos_source": "pending",
    }
    if reference:
        row["matches_reference_packet"] = packet == reference["payload_sha256"]
        row["matches_reference_pcm"] = pcm == reference["decoded_sha256"]
    return row


def validate_off_controls(result_rows: dict) -> None:
    for clip in CLIPS:
        for rate in RATES:
            for label, reference_label in (("off_base", "baseline_ref"), ("off_stack", "stack_ref")):
                row = result_rows[f"{label}|{clip}|{rate}"]
                ref = result_rows[f"{reference_label}|{clip}|{rate}"]
                if row["payload_sha256"] != ref["payload_sha256"] or row["decoded_sha256"] != ref["decoded_sha256"]:
                    raise RuntimeError(f"Mode-0 exact control failed before MOS: {label}|{clip}|{rate}")
                row["mos"] = ref["mos"]
                row["metric_backend"] = ref["metric_backend"]
                row["mos_source"] = f"identity:{reference_label}|{clip}|{rate}"


def score_feature(ec, row: dict, reference: dict, ref_wav: Path, scorer) -> None:
    if row["payload_sha256"] == reference["payload_sha256"] and row["decoded_sha256"] == reference["decoded_sha256"]:
        row["mos"] = reference["mos"]
        row["metric_backend"] = reference["metric_backend"]
        row["mos_source"] = f"identity:{reference['variant']}|{row['clip']}|{row['target_kbps']}"
    else:
        mos, backend = scorer(str(ref_wav), row["decoded_path"], mode_str="audio", sample_rate=48000)
        if mos is None or backend != "zimtohrli" or not math.isfinite(float(mos)):
            raise RuntimeError(f"Zimtohrli scoring failed for {row['variant']}|{row['clip']}|{row['target_kbps']}: {mos}, {backend}")
        row["mos"], row["metric_backend"], row["mos_source"] = float(mos), backend, "scored"


def validate_interop(ec, rows: dict, out_dir: Path, ffmpeg: str) -> dict:
    validation = []
    for key, row in sorted(rows.items()):
        encoded = Path(row["encoded_path"])
        with tempfile.TemporaryDirectory(prefix="border-interop-") as temp:
            faad_wav = Path(temp) / (encoded.stem + ".wav")
            faad = subprocess.run([str(FAAD_BIN), "--strict", "--quiet", "--overwrite", "-o", str(faad_wav), str(encoded)],
                                  capture_output=True, text=True)
            ff = subprocess.run([ffmpeg, "-v", "error", "-y", "-i", str(encoded), "-f", "null", "-"],
                                capture_output=True, text=True)
            item = {"key": key, "faad_ok": faad.returncode == 0 and faad_wav.is_file() and faad_wav.stat().st_size > 0,
                    "faad_returncode": faad.returncode, "ffmpeg_ok": ff.returncode == 0,
                    "ffmpeg_returncode": ff.returncode}
            validation.append(item)
            if not item["faad_ok"] or not item["ffmpeg_ok"]:
                atomic_json(out_dir / "interop-results.json", {"streams": validation})
                raise RuntimeError(f"Decoder validation failed: {key}")
    summary = {"streams": len(validation), "faad_pass": sum(v["faad_ok"] for v in validation),
               "ffmpeg_pass": sum(v["ffmpeg_ok"] for v in validation), "rows": validation}
    atomic_json(out_dir / "interop-results.json", summary)
    return summary


def summarize(ec, rows: dict, out_dir: Path, bootstrap_mean_ci, bd_rate_curve) -> dict:
    comparisons = {}
    for label, flags in feature_matrix().items():
        if label.startswith("off_"):
            continue
        reference_label = "stack_ref" if flags["FAAC_SBR_ENVCOUNT_STACK"] == "1" else "baseline_ref"
        per_clip = []
        per_rate = {str(rate): [] for rate in RATES}
        for clip in CLIPS:
            candidate_points = [(rows[f"{label}|{clip}|{rate}"]["actual_payload_kbps"],
                                rows[f"{label}|{clip}|{rate}"]["mos"]) for rate in RATES]
            reference_points = [(rows[f"{reference_label}|{clip}|{rate}"]["actual_payload_kbps"],
                                 rows[f"{reference_label}|{clip}|{rate}"]["mos"]) for rate in RATES]
            value = bd_rate_curve(reference_points, candidate_points)
            if value is not None:
                per_clip.append({"clip": clip, "bd_rate_percent": value})
            for rate in RATES:
                a, b = rows[f"{label}|{clip}|{rate}"], rows[f"{reference_label}|{clip}|{rate}"]
                per_rate[str(rate)].append({"clip": clip, "mos_delta": a["mos"] - b["mos"],
                                            "payload_kbps_delta": a["actual_payload_kbps"] - b["actual_payload_kbps"]})
        vals = [item["bd_rate_percent"] for item in per_clip]
        comparisons[label] = {
            "reference": reference_label, "mean_bd_rate_percent": sum(vals) / len(vals),
            "median_bd_rate_percent": statistics.median(vals),
            "clip_bootstrap_95_ci": bootstrap_mean_ci(vals), "valid_clips": len(vals),
            "per_clip_bd_rate": per_clip,
            "per_rate": {rate: {"mean_mos_delta": sum(x["mos_delta"] for x in data) / len(data),
                                "mean_payload_kbps_delta": sum(x["payload_kbps_delta"] for x in data) / len(data),
                                "mos_delta_bootstrap_95_ci": bootstrap_mean_ci([x["mos_delta"] for x in data]),
                                "worst_mos_deltas": sorted(data, key=lambda x: x["mos_delta"]) }
                         for rate, data in per_rate.items()},
        }
    result = {"schema": 1, "clips": list(CLIPS), "rates_kbps": list(RATES),
              "matrix_rows": len(rows), "comparisons": comparisons,
              "interpretation": "Four-clip gate only; no production winner claim. Full follow-up only if root approves after review."}
    atomic_json(out_dir / "summary.json", result)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--start", action="store_true", help="run the full gate matrix; requires reviewed source and root GO")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--output-dir", type=Path, default=OUTPUT)
    args = parser.parse_args()
    ec = envcount_module()
    for binary in (BASE_BIN, BASE_LIB, STACK_BIN, STACK_LIB, PROBE_BIN, PROBE_LIB, PROBE_PATCH, FDK_BIN, FAAD_BIN):
        if not binary.is_file():
            raise FileNotFoundError(binary)
    ffmpeg = shutil.which("ffmpeg")
    if not FFMPEG:
        raise FileNotFoundError("ffmpeg required")
    ffmpeg = FFMPEG
    out_dir = args.output_dir.expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    identity = build_identity(ec, Path(__file__).resolve())
    config_path = out_dir / "config.json"
    old = json.loads(config_path.read_text()) if config_path.exists() else None
    if old and old.get("immutable_run_config") != identity:
        raise RuntimeError("Immutable source/binary/input/runner configuration changed")
    config = {"schema": 1, "created_utc": datetime.now(timezone.utc).isoformat(),
              "immutable_run_config": identity, "scoring_enabled": bool(args.start),
              "workers": args.jobs, "output_dir": str(out_dir)}
    atomic_json(config_path, config)
    if not args.start:
        print(f"Prepared transient-border gate config only: {config_path}")
        print("No AAC streams, MOS scores, or decoder runs were started; source and quality gate authorized; use this prepared final-source config.")
        return 0

    sys.path.insert(0, str(BENCH)); sys.path.insert(0, str(BENCH / "scripts"))
    from codec_bench.encoders import FAACEncoder
    from codec_bench.decoders import FDKDecoder
    import utils
    from compare_results import bootstrap_mean_ci
    from phase2_mos import score_wav_pair
    from bd_rate import bd_rate_curve

    base_doc, stack_doc = json.loads(BASE_RESULTS.read_text()), json.loads(STACK_RESULTS.read_text())
    rows = make_reference_rows(ec, base_doc, stack_doc, {})
    ref_wav_dir = out_dir / "reference-wav"
    ref_wav_dir.mkdir(parents=True, exist_ok=True)
    refs = {}
    for clip in CLIPS:
        path = ref_wav_dir / clip
        source = BENCH / "data/external/audio" / clip
        if not path.is_file() and not utils.wav_conv(str(source), str(path), rate=48000, channels=2):
            raise RuntimeError(f"Failed to prepare reference PCM {clip}")
        refs[clip] = path
    result_path = out_dir / "rows.json"
    old_rows = json.loads(result_path.read_text())["rows"] if result_path.exists() else {}
    rows.update(old_rows)
    atomic_json(result_path, {"schema": 1, "rows": rows})

    # Establish all 32 exact mode-off controls before any MOS scoring.
    off_rows = {}
    off_jobs = [(label, feature_matrix()[label], clip, rate)
                for label in ("off_base", "off_stack") for clip in CLIPS for rate in RATES]
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {pool.submit(encode_decode, ec, label, flags, None, clip, rate, out_dir, FAACEncoder,
                               FDKDecoder, utils): (label, clip, rate)
                   for label, flags, clip, rate in off_jobs}
        for future in as_completed(futures):
            label, clip, rate = futures[future]
            row = future.result(); off_rows[f"{label}|{clip}|{rate}"] = row
    rows.update(off_rows)
    atomic_json(result_path, {"schema": 1, "rows": rows})
    validate_off_controls(rows)
    atomic_json(result_path, {"schema": 1, "rows": rows})

    feature_jobs = [(label, flags, clip, rate)
                    for label, flags in feature_matrix().items() if not label.startswith("off_")
                    for clip in CLIPS for rate in RATES]
    feature_rows = {}
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {pool.submit(encode_decode, ec, label, flags, None, clip, rate, out_dir, FAACEncoder,
                               FDKDecoder, utils): (label, flags, clip, rate)
                   for label, flags, clip, rate in feature_jobs}
        for future in as_completed(futures):
            label, flags, clip, rate = futures[future]
            row = future.result()
            ref_label = "stack_ref" if flags["FAAC_SBR_ENVCOUNT_STACK"] == "1" else "baseline_ref"
            score_feature(ec, row, rows[f"{ref_label}|{clip}|{rate}"], refs[clip], score_wav_pair)
            feature_rows[f"{label}|{clip}|{rate}"] = row
            print(f"completed {label}|{clip}|{rate}: payload={row['actual_payload_kbps']:.3f}kbps MOS={row['mos']:.6f} source={row['mos_source']}", flush=True)
    rows.update(feature_rows)
    atomic_json(result_path, {"schema": 1, "rows": rows})
    expected_rows = (2 + len(feature_matrix())) * len(CLIPS) * len(RATES)
    if len(rows) != expected_rows:
        raise RuntimeError(f"Expected {expected_rows} matrix rows, got {len(rows)}")
    interop = validate_interop(ec, rows, out_dir, ffmpeg)
    summary = summarize(ec, rows, out_dir, bootstrap_mean_ci, bd_rate_curve)
    summary["interop"] = {k: interop[k] for k in ("streams", "faad_pass", "ffmpeg_pass")}
    atomic_json(out_dir / "summary.json", summary)
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
