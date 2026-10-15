#!/usr/bin/env python3
"""Score unconditional steady FIXFIX 1/2-envelope controls against the active study."""

from __future__ import annotations

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
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime, timezone
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import run_envcount as envcount

BENCH = envcount.BENCH
OUT = envcount.DEFAULT_OUTPUT_ROOT / "fixed-count-controls" / "full"
FULL_RESULTS = envcount.DEFAULT_OUTPUT_ROOT / "full-active-lambda" / "envcount-results.json"
RATE_KBPS = (32, 48, 64, 96)
CONTROL_BINS = {
    "base_fixed1": Path("/private/tmp/faac-envcount-fixed1-base/build-fixed/frontend/faac"),
    "base_fixed2": Path("/private/tmp/faac-envcount-fixed2-base/build-fixed/frontend/faac"),
    "stack_fixed1": Path("/private/tmp/faac-envcount-fixed1-stack/build-fixed/frontend/faac"),
    "stack_fixed2": Path("/private/tmp/faac-envcount-fixed2-stack/build-fixed/frontend/faac"),
}
CONTROL_WORKTREES = {
    "base_fixed1": Path("/private/tmp/faac-envcount-fixed1-base"),
    "base_fixed2": Path("/private/tmp/faac-envcount-fixed2-base"),
    "stack_fixed1": Path("/private/tmp/faac-envcount-fixed1-stack"),
    "stack_fixed2": Path("/private/tmp/faac-envcount-fixed2-stack"),
}
EXPECTED_HASHES = {
    "base_fixed1": ("c0dc1e7420c70657307af24d866786c3d15e37fee919834d43cd37157d7529b9",
                     "6c1c5ccb22876e881b4704608d5a475f133f0902058426d8260e4db1d7f4b5e5"),
    "base_fixed2": ("c0dc1e7420c70657307af24d866786c3d15e37fee919834d43cd37157d7529b9",
                     "d74ddbfb2ef5d99f60cc2f0b54deb50004a10dfdfac213f48946f2eca9bb37bb"),
    "stack_fixed1": ("7741458a69d12c7ceabd7e0b2b3e60f85f74620f29a28c7be49fb2f1c6b32d5d",
                     "e8069b77feeb21cf7dfa5a950bd63a92e398bc0a579c084c0e471562cea04d94"),
    "stack_fixed2": ("7741458a69d12c7ceabd7e0b2b3e60f85f74620f29a28c7be49fb2f1c6b32d5d",
                     "5cd28bf8ba48da6ec7ec60bd119fdf1b5e954fb979fd7480cf426138252db700"),
}


def binary_library(binary: Path) -> Path:
    return binary.parents[1] / "libfaac/libfaac.2.dylib"


def atomic_json(path: Path, data) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n")
    os.replace(tmp, path)


def main() -> int:
    sys.path.insert(0, str(BENCH))
    sys.path.insert(0, str(BENCH / "scripts"))
    import utils
    from codec_bench.decoders import FDKDecoder
    from codec_bench.encoders import FAACEncoder
    from compare_results import bootstrap_mean_ci
    from phase2_mos import score_wav_pair
    from bd_rate import bd_rate_curve

    if not FULL_RESULTS.is_file():
        raise FileNotFoundError(FULL_RESULTS)
    full_doc = json.loads(FULL_RESULTS.read_text())
    full_rows = full_doc["rows"]
    baseline_prior = json.loads((envcount.BASE_RUN_ROOT / "results-all/feature-results.json").read_text())["rows"]
    stack_prior = json.loads((envcount.STACK_RUN_ROOT / "results-all/stack-results.json").read_text())["rows"]
    for binary in CONTROL_BINS.values():
        if not binary.is_file() or not binary_library(binary).is_file():
            raise FileNotFoundError(binary)
    for name, binary in CONTROL_BINS.items():
        frontend_hash, library_hash = EXPECTED_HASHES[name]
        if envcount.sha256(binary) != frontend_hash or envcount.sha256(binary_library(binary)) != library_hash:
            raise RuntimeError(f"Fixed-control binary/library hash does not match reviewed manifest: {name}")

    clips = sorted(path.name for path in (BENCH / "data/external/audio").glob("*.wav"))
    if len(clips) != 49:
        raise RuntimeError(f"Expected 49 full clips, got {len(clips)}")
    source_dir = BENCH / "data/external/audio"
    out_dir = OUT
    enc_dir, wav_dir = out_dir / "encodes", out_dir / "decoded"
    enc_dir.mkdir(parents=True, exist_ok=True)
    wav_dir.mkdir(parents=True, exist_ok=True)
    ref_wav_dir = out_dir / "reference-wav"
    ref_wav_dir.mkdir(parents=True, exist_ok=True)
    for clip in clips:
        source = source_dir / clip
        with wave.open(str(source), "rb") as reader:
            if reader.getframerate() != 48000 or reader.getnchannels() != 2:
                raise RuntimeError(f"Expected 48 kHz stereo input: {source}")
    config_identity = {
        "runner_sha256": envcount.sha256(Path(__file__).resolve()),
        "encoder_source_head": envcount.git_head(envcount.PROBE),
        "encoder_source_diff_sha256": envcount.source_diff_hash(envcount.PROBE),
        "active_probe_binary_sha256": envcount.sha256(envcount.CAND_BIN),
        "baseline_binary_sha256": envcount.sha256(envcount.BASE_BIN),
        "stack_reference_binary_sha256": envcount.sha256(envcount.STACK_REF_BIN),
        "control_binaries": {name: {"path": str(binary), "sha256": envcount.sha256(binary),
                                     "library_path": str(binary_library(binary)),
                                     "library_sha256": envcount.sha256(binary_library(binary))}
                             for name, binary in CONTROL_BINS.items()},
        "control_worktrees": {name: {"path": str(worktree),
                                      "head": envcount.git_head(worktree),
                                      "source_diff_sha256": envcount.source_diff_hash(worktree)}
                              for name, worktree in CONTROL_WORKTREES.items()},
        "full_results_sha256": envcount.sha256(FULL_RESULTS),
        "clips": clips,
        "rates_kbps": list(RATE_KBPS),
        "scoring": "Zimtohrli audio mode at 48 kHz, strict FDK decode first, actual AAC packet bitrate",
        "scoring_performed": True,
    }
    config_path = out_dir / "config.json"
    if config_path.exists():
        existing = json.loads(config_path.read_text())
        if existing.get("immutable_run_config") != config_identity:
            raise RuntimeError("Fixed-control output has a different immutable provenance")
    else:
        atomic_json(config_path, {"created_utc": datetime.now(timezone.utc).isoformat(),
                                  "immutable_run_config": config_identity})

    result_path = out_dir / "fixed-results.json"
    results = json.loads(result_path.read_text()) if result_path.exists() else {"schema": 1, "rows": {}}
    rows = results["rows"]
    lock = threading.Lock()
    decoder = FDKDecoder("FDK AAC", str(envcount.FDK_BIN))
    for clip in clips:
        source = source_dir / clip
        ref = ref_wav_dir / clip
        if not ref.is_file() and not utils.wav_conv(str(source), str(ref), rate=48000, channels=2):
            raise RuntimeError(f"Cannot prepare reference WAV for {source}")

    def process(control: str, clip: str, rate: int) -> dict:
        binary = CONTROL_BINS[control]
        source = source_dir / clip
        encoded = enc_dir / control / str(rate) / (Path(clip).stem + ".m4a")
        decoded = wav_dir / control / str(rate) / (Path(clip).stem + ".wav")
        encoded.parent.mkdir(parents=True, exist_ok=True)
        decoded.parent.mkdir(parents=True, exist_ok=True)
        key = f"{control}|{clip}|{rate}"
        cached = rows.get(key)
        valid_cache = (cached and encoded.is_file() and decoded.is_file()
                       and cached.get("encoded_sha256") == envcount.sha256(encoded)
                       and cached.get("decoded_sha256") == envcount.sha256(decoded))
        if not valid_cache:
            encoded.unlink(missing_ok=True)
            decoded.unlink(missing_ok=True)
            encoder = FAACEncoder("faac-fixed-envelope-control", str(binary), profile="he")
            command = encoder.get_encode_cmd(str(source), str(encoded), rate, 2, 48000)
            if "--object-type" not in command or command[command.index("--object-type") + 1] != "he-aac-v1":
                raise RuntimeError(f"Control command does not force HE-AAC v1: {command}")
            env = {key: value for key, value in os.environ.items()
                   if not key.startswith("FAAC_SBR_")}
            completed = envcount.run(command, env=env)
            if envcount.profile(encoded) != "HE-AAC":
                raise RuntimeError(f"Expected HE-AAC profile: {key}")
            with tempfile.TemporaryDirectory(prefix="envcount-fixed-fdk-") as temp:
                envcount.decode_fdk(encoded, decoded, decoder, utils, Path(temp))
            packet_hash = envcount.payload_identity(encoded)
            pcm_hash = envcount.sha256(decoded)
            actual = envcount.payload_bitrate(encoded, envcount.wav_duration(source))
            command_used, stderr_tail = command, completed.stderr[-1000:]
        else:
            packet_hash = cached["payload_sha256"]
            pcm_hash = cached["decoded_sha256"]
            actual = cached["actual_payload_kbps"]
            command_used, stderr_tail = cached.get("command", []), cached.get("stderr_tail", "")

        context = "stack_reference" if control.startswith("stack_") else "baseline_original"
        reference = full_rows[f"{context}|{clip}|{rate}"]
        ref_path, ref_decoded = Path(reference["encoded_path"]), Path(reference["decoded_path"])
        identity_match = (packet_hash == envcount.payload_identity(ref_path)
                          and pcm_hash == envcount.sha256(ref_decoded))
        source_mos = None
        if identity_match:
            mos, backend = reference["mos"], reference.get("metric_backend", "zimtohrli")
            source_mos = f"identity:{context}|{clip}|{rate}"
        else:
            mos, backend = score_wav_pair(str(ref_wav_dir / clip), str(decoded),
                                          mode_str="audio", sample_rate=48000)
            if mos is None or backend != "zimtohrli" or not math.isfinite(float(mos)):
                raise RuntimeError(f"Strict Zimtohrli scoring failed: {key}: {mos}, {backend}")
            source_mos = "scored"
        row = {
            "variant": control, "clip": clip, "target_kbps": rate,
            "actual_payload_kbps": actual, "profile": "HE-AAC",
            "mos": float(mos), "metric_backend": backend, "mos_source": source_mos,
            "matched_reference": context, "packet_pcm_identity_match": identity_match,
            "payload_sha256": packet_hash, "decoded_sha256": pcm_hash,
            "encoded_path": str(encoded), "encoded_sha256": envcount.sha256(encoded),
            "decoded_path": str(decoded), "decoded_sha256": envcount.sha256(decoded),
            "command": command_used, "stderr_tail": stderr_tail,
        }
        with lock:
            rows[key] = row
            atomic_json(result_path, results)
        print(f"completed {key}: MOS={mos:.6f} actual={actual:.3f} kbps source={source_mos}", flush=True)
        return row

    with ThreadPoolExecutor(max_workers=4) as pool:
        futures = [pool.submit(process, control, clip, rate)
                   for control in CONTROL_BINS for clip in clips for rate in RATE_KBPS]
        for future in as_completed(futures):
            future.result()

    # Strict FAAD3 and FFmpeg validation follows scoring for each unique control stream.
    interop = []
    for key, row in sorted(rows.items()):
        encoded = Path(row["encoded_path"])
        with tempfile.TemporaryDirectory(prefix="envcount-fixed-interop-") as temp:
            faad_wav = Path(temp) / (encoded.stem + ".wav")
            faad = subprocess.run([str(envcount.FAAD_BIN), "--strict", "--quiet", "--overwrite",
                                   "-o", str(faad_wav), str(encoded)], capture_output=True, text=True)
            ffmpeg = subprocess.run([envcount.FFMPEG, "-v", "error", "-y", "-i", str(encoded),
                                     "-f", "null", "-"], capture_output=True, text=True)
            item = {"key": key, "faad_ok": faad.returncode == 0 and faad_wav.is_file() and faad_wav.stat().st_size > 0,
                    "faad_returncode": faad.returncode, "ffmpeg_ok": ffmpeg.returncode == 0,
                    "ffmpeg_returncode": ffmpeg.returncode}
            interop.append(item)
            if not item["faad_ok"] or not item["ffmpeg_ok"]:
                raise RuntimeError(f"Fixed-control decoder interop failure: {key}")
    atomic_json(out_dir / "interop-results.json", {
        "streams": len(interop), "faad_pass": sum(row["faad_ok"] for row in interop),
        "ffmpeg_pass": sum(row["ffmpeg_ok"] for row in interop), "rows": interop})

    # Matched base/stack BD-rate for each fixed control, plus adaptive-vs-fixed
    # comparisons to reveal whether adaptation beats a constant steady count.
    comparisons = {}
    for control in CONTROL_BINS:
        context = "stack_reference" if control.startswith("stack_") else "baseline_original"
        per_clip = []
        for clip in clips:
            candidate_points = [(rows[f"{control}|{clip}|{rate}"]["actual_payload_kbps"],
                                 rows[f"{control}|{clip}|{rate}"]["mos"]) for rate in RATE_KBPS]
            reference_points = [(full_rows[f"{context}|{clip}|{rate}"]["actual_payload_kbps"],
                                 full_rows[f"{context}|{clip}|{rate}"]["mos"]) for rate in RATE_KBPS]
            value = bd_rate_curve(reference_points, candidate_points)
            if value is not None:
                per_clip.append({"clip": clip, "bd_rate_percent": value})
        values = [item["bd_rate_percent"] for item in per_clip]
        ci = bootstrap_mean_ci(values)
        comparisons[f"{control}_vs_{context}"] = {
            "mean_bd_rate_percent": statistics.mean(values),
            "median_bd_rate_percent": statistics.median(values),
            "mean_bd_rate_bootstrap_95_ci": ci,
            "valid_clips": len(values), "per_clip_bd_rate": per_clip,
        }

    adaptive_doc = json.loads((out_dir.parent.parent / "full-active-lambda/envcount-results.json").read_text())
    adaptive_rows = adaptive_doc["rows"]
    adaptive_vs_fixed = {}
    for adaptive in ("ad001_base", "ad003_base", "ad001_stack", "ad003_stack"):
        context = "stack_reference" if adaptive.endswith("_stack") else "baseline_original"
        for fixed_n in (1, 2):
            fixed = ("stack" if adaptive.endswith("_stack") else "base") + f"_fixed{fixed_n}"
            per_clip = []
            regressions_32 = []
            for clip in clips:
                adaptive_points = [(adaptive_rows[f"{adaptive}|{clip}|{rate}"]["actual_payload_kbps"],
                                    adaptive_rows[f"{adaptive}|{clip}|{rate}"]["mos"]) for rate in RATE_KBPS]
                fixed_points = [(rows[f"{fixed}|{clip}|{rate}"]["actual_payload_kbps"],
                                 rows[f"{fixed}|{clip}|{rate}"]["mos"]) for rate in RATE_KBPS]
                value = bd_rate_curve(fixed_points, adaptive_points)
                if value is not None:
                    per_clip.append({"clip": clip, "bd_rate_percent": value})
                a32, f32 = adaptive_rows[f"{adaptive}|{clip}|32"], rows[f"{fixed}|{clip}|32"]
                regressions_32.append({"clip": clip, "mos_delta": a32["mos"] - f32["mos"],
                                       "actual_bitrate_delta_kbps": a32["actual_payload_kbps"] - f32["actual_payload_kbps"]})
            values = [item["bd_rate_percent"] for item in per_clip]
            ci = bootstrap_mean_ci(values)
            regressions_32.sort(key=lambda item: item["mos_delta"])
            adaptive_vs_fixed[f"{adaptive}_vs_{fixed}"] = {
                "mean_bd_rate_percent": statistics.mean(values),
                "median_bd_rate_percent": statistics.median(values),
                "mean_bd_rate_bootstrap_95_ci": ci,
                "valid_clips": len(values), "per_clip_bd_rate": per_clip,
                "worst_32_kbps_mos_regressions": regressions_32[:10],
            }
    atomic_json(out_dir / "comparisons.json", {
        "schema": 1, "clips": clips, "rates_kbps": list(RATE_KBPS),
        "matched_fixed_controls": comparisons, "adaptive_vs_fixed1_fixed2": adaptive_vs_fixed,
        "decoder_validation": {"streams": len(interop), "faad_pass": sum(r["faad_ok"] for r in interop),
                               "ffmpeg_pass": sum(r["ffmpeg_ok"] for r in interop)},
        "source_note": "D for adaptive settings is a quantized-envelope surrogate, not decoded waveform distortion or exact RD.",
    })
    print(f"Saved fixed-control comparisons: {out_dir / 'comparisons.json'}")
    return 0


if __name__ == "__main__":
    sys.path.insert(0, str(BENCH))
    raise SystemExit(main())
