#!/usr/bin/env python3
"""Matched native-ARM64 serial timing for baseline and production HE-AAC v1."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import statistics
import subprocess
import sys
import tempfile
import wave
from datetime import datetime, timezone
from pathlib import Path

BENCH = Path("/Users/nschimme/gitprojects/faac-benchmark")
BASE_BIN = Path("/private/tmp/faac-he-v1-probes/build-baseline/frontend/faac")
BASE_LIB = Path("/private/tmp/faac-he-v1-probes/build-baseline/libfaac/libfaac.2.dylib")
CANDIDATE_BIN = Path("/private/tmp/faac-he-v1-candidate/build-candidate/frontend/faac")
CANDIDATE_LIB = Path("/private/tmp/faac-he-v1-candidate/build-candidate/libfaac/libfaac.2.dylib")
STACK_ARCHIVE = Path("/Users/nschimme/gitprojects/faac/.cache/he-v1-feature-study/stack")
STACK_RESULTS = STACK_ARCHIVE / "results-all/stack-results.json"
DEFAULT_OUTPUT = STACK_ARCHIVE / "timing-serial"
CLIPS = ("sandman.16b48k.wav", "fms.wav")
MIN_DURATION_SECONDS = 120
PAIRS_PER_CLIP = 20
TARGET_KBPS = 64
PROBE_PREFIX = "FAAC_SBR_"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def git_head(path: Path) -> str:
    return subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()


def linked_libraries(binary: Path) -> list[dict]:
    output = subprocess.check_output(["otool", "-L", str(binary)], text=True)
    libraries = []
    for line in output.splitlines()[1:]:
        name, _, version = line.strip().partition(" (compatibility version")
        actual = BASE_LIB if binary == BASE_BIN and name == "@rpath/libfaac.2.dylib" else (
            CANDIDATE_LIB if binary == CANDIDATE_BIN and name == "@rpath/libfaac.2.dylib" else None)
        libraries.append({
            "install_name": name,
            "sha256": sha256(actual) if actual else None,
            "resolved_build_path": str(actual) if actual else None,
            "version_info": version,
        })
    return libraries


def prepare_repeated_pcm(source: Path, output: Path) -> dict:
    with wave.open(str(source), "rb") as reader:
        params = reader.getparams()
        if reader.getframerate() != 48000 or reader.getnchannels() != 2:
            raise RuntimeError(f"Expected native 48 kHz stereo input: {source}")
        frames = reader.getnframes()
        pcm = reader.readframes(frames)
    frame_bytes = params.nchannels * params.sampwidth
    if len(pcm) != frames * frame_bytes:
        raise RuntimeError(f"Unexpected PCM length in {source}")
    duration = frames / params.framerate
    repeats = math.ceil(MIN_DURATION_SECONDS / duration)
    repeated = pcm * repeats
    repeated_frames = frames * repeats
    output.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(output), "wb") as writer:
        writer.setparams(params)
        writer.writeframes(repeated)
    with wave.open(str(output), "rb") as reader:
        output_params = reader.getparams()
        if (output_params.nchannels != params.nchannels or
                output_params.sampwidth != params.sampwidth or
                output_params.framerate != params.framerate or
                output_params.comptype != params.comptype or
                output_params.compname != params.compname or
                reader.getnframes() != repeated_frames):
            raise RuntimeError(f"Repeated WAV metadata changed: {output}")
        check = reader.readframes(repeated_frames)
    if check != repeated:
        raise RuntimeError(f"Repeated WAV PCM differs from direct concatenation: {output}")
    return {
        "source_path": str(source),
        "source_sha256": sha256(source),
        "source_pcm_sha256": hashlib.sha256(pcm).hexdigest(),
        "source_frames": frames,
        "source_duration_seconds": duration,
        "repeat_count": repeats,
        "sample_rate_hz": params.framerate,
        "channels": params.nchannels,
        "sample_width_bytes": params.sampwidth,
        "repeated_frames": repeated_frames,
        "repeated_duration_seconds": repeated_frames / params.framerate,
        "repeated_pcm_sha256": hashlib.sha256(repeated).hexdigest(),
        "repeated_wav_path": str(output),
        "repeated_wav_sha256": sha256(output),
        "pcm_method": "direct concatenation of original interleaved PCM frames; no resampling or format conversion",
    }


def ffprobe_profile(path: Path) -> str:
    result = subprocess.run(
        ["ffprobe", "-v", "error", "-select_streams", "a:0", "-show_entries",
         "stream=profile", "-of", "json", str(path)],
        check=True, capture_output=True, text=True)
    streams = json.loads(result.stdout).get("streams", [])
    if len(streams) != 1:
        raise RuntimeError(f"Expected one audio stream in {path}")
    return streams[0].get("profile", "")


def bootstrap_mean_ci(values: list[float]) -> dict | None:
    sys.path.insert(0, str(BENCH))
    from compare_results import bootstrap_mean_ci as benchmark_bootstrap_mean_ci
    return benchmark_bootstrap_mean_ci(values)


def atomic_json(path: Path, data: dict) -> None:
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n")
    os.replace(tmp, path)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--start", action="store_true",
                        help="begin the timing run; omit this flag to create only prepared config and inputs")
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()
    out_dir = args.output_dir.expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    os.environ.setdefault("NUMBA_DISABLE_JIT", "1")
    sys.path.insert(0, str(BENCH))
    from codec_bench.encoders import FAACEncoder
    import utils
    sys.path.insert(0, str(BENCH / "scripts"))

    source_dir = BENCH / "data/external/audio"
    input_dir = out_dir / "inputs"
    inputs = {clip: prepare_repeated_pcm(source_dir / clip, input_dir / clip)
              for clip in CLIPS}
    base_bin_hash = sha256(BASE_BIN)
    candidate_bin_hash = sha256(CANDIDATE_BIN)
    base_lib_hash = sha256(BASE_LIB)
    candidate_lib_hash = sha256(CANDIDATE_LIB)
    stack_rows = json.loads(STACK_RESULTS.read_text())["rows"]
    expected_stack = [row for key, row in stack_rows.items() if key.startswith("stack18|")]
    if len(expected_stack) != 196:
        raise RuntimeError(f"Expected 196 exact production-equivalence rows, got {len(expected_stack)}")
    equivalence_path = STACK_ARCHIVE / "production-equivalence/production-equivalence.json"
    equivalence = json.loads(equivalence_path.read_text())
    if equivalence["counts"]["both_matches"] != 196:
        raise RuntimeError("Production packet/PCM equivalence gate is incomplete")
    config = {
        "schema": 1,
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "host": {"system": subprocess.check_output(["uname", "-a"], text=True).strip(),
                 "architecture": subprocess.check_output(["uname", "-m"], text=True).strip()},
        "method": "native serial encoder timing with faac-benchmark utils.measure_peak_ram; do not overlap other quality/decoder jobs",
        "benchmark_commit": git_head(BENCH),
        "baseline_source_commit": git_head(BASE_BIN.parents[2]),
        "candidate_source_commit": git_head(CANDIDATE_BIN.parents[2]),
        "baseline_binary": {"path": str(BASE_BIN), "sha256": base_bin_hash,
                            "linked_libraries": linked_libraries(BASE_BIN)},
        "candidate_binary": {"path": str(CANDIDATE_BIN), "sha256": candidate_bin_hash,
                             "linked_libraries": linked_libraries(CANDIDATE_BIN)},
        "libfaac_dylibs": {
            str(BASE_LIB): {"sha256": base_lib_hash},
            str(CANDIDATE_LIB): {"sha256": candidate_lib_hash},
        },
        "stack18_reference_results": {"path": str(STACK_RESULTS), "sha256": sha256(STACK_RESULTS)},
        "production_equivalence": {"path": str(equivalence_path), "sha256": sha256(equivalence_path),
                                   "rows": equivalence["counts"]},
        "clips": inputs,
        "target_kbps": TARGET_KBPS,
        "profile": "HE-AAC v1; command must force he-aac-v1 and ffprobe must report HE-AAC",
        "pairs_per_clip": PAIRS_PER_CLIP,
        "pair_order": "20 paired observations per clip, alternating baseline/candidate then candidate/baseline",
        "environment": "All FAAC_SBR_* probe variables are removed; each encode is a separate measure_peak_ram invocation",
        "timing_metric": "measure_peak_ram returned encoder subprocess duration_seconds",
        "peak_ram_metric": "measure_peak_ram returned peak_rss_kb, measured separately from duration; on macOS helper converts ru_maxrss bytes to KiB",
        "start_requested": bool(args.start),
    }
    config_path = out_dir / "timing-config.json"
    atomic_json(config_path, config)
    if not args.start:
        print(f"Prepared timing config and exact repeated PCM inputs: {config_path}")
        print("No timing encodes were started; pass --start only after the coordinator confirms all other runs are terminal.")
        return 0
    result_path = out_dir / "timing-results.json"
    if result_path.exists():
        raise RuntimeError(f"Refusing to overwrite an existing timing run: {result_path}")

    encoders = {
        "baseline": FAACEncoder("faac-baseline", str(BASE_BIN), profile="he"),
        "candidate": FAACEncoder("faac-production-stack", str(CANDIDATE_BIN), profile="he"),
    }
    run = {"schema": 1, "config_sha256": sha256(config_path), "timing_rows": [], "complete": False}
    for clip in CLIPS:
        source = Path(inputs[clip]["repeated_wav_path"])
        for pair_index in range(PAIRS_PER_CLIP):
            order = ["baseline", "candidate"] if pair_index % 2 == 0 else ["candidate", "baseline"]
            measurements = {}
            for label in order:
                encoder = encoders[label]
                with tempfile.TemporaryDirectory(prefix=f"serial-{label}-", dir=out_dir) as tmpdir:
                    output = Path(tmpdir) / f"{clip}.{pair_index + 1}.m4a"
                    command = encoder.get_encode_cmd(str(source), str(output), TARGET_KBPS, 2, 48000)
                    if "--object-type" not in command or command[command.index("--object-type") + 1] != "he-aac-v1":
                        raise RuntimeError(f"Command does not force HE-AAC v1: {command}")
                    env = dict(os.environ)
                    for key in list(env):
                        if key.startswith(PROBE_PREFIX):
                            env.pop(key)
                    proc, duration, rss_kb = utils.measure_peak_ram(command, env=env, check=True, timeout=120)
                    if proc.returncode != 0 or not output.is_file():
                        raise RuntimeError(f"Encode failed: {label} {clip} pair {pair_index + 1}")
                    profile = ffprobe_profile(output)
                    if profile != "HE-AAC":
                        raise RuntimeError(f"Expected HE-AAC profile, got {profile!r}: {label} {clip}")
                    measurements[label] = {
                        "command": command,
                        "profile": profile,
                        "duration_seconds": duration,
                        "peak_rss_kb": rss_kb,
                        "output_size_bytes": output.stat().st_size,
                        "output_sha256": sha256(output),
                        "execution_order_index": order.index(label),
                    }
            duration_delta = 100.0 * (measurements["candidate"]["duration_seconds"] /
                                      measurements["baseline"]["duration_seconds"] - 1.0)
            rss_delta = 100.0 * (measurements["candidate"]["peak_rss_kb"] /
                                 measurements["baseline"]["peak_rss_kb"] - 1.0)
            run["timing_rows"].append({
                "clip": clip,
                "pair": pair_index + 1,
                "execution_order": order,
                "measurements": measurements,
                "candidate_vs_baseline_duration_delta_percent": duration_delta,
                "candidate_vs_baseline_peak_rss_delta_percent": rss_delta,
            })
            atomic_json(result_path, run)
            print(f"{clip} pair {pair_index + 1}/{PAIRS_PER_CLIP}: "
                  f"duration {duration_delta:+.3f}%, peak RSS {rss_delta:+.3f}%", flush=True)

    summaries = {}
    for clip in CLIPS:
        rows = [r for r in run["timing_rows"] if r["clip"] == clip]
        deltas = [r["candidate_vs_baseline_duration_delta_percent"] for r in rows]
        rss = [r["candidate_vs_baseline_peak_rss_delta_percent"] for r in rows]
        ci = bootstrap_mean_ci(deltas)
        summaries[clip] = {
            "pairs": len(rows),
            "mean_duration_delta_percent": statistics.mean(deltas),
            "median_duration_delta_percent": statistics.median(deltas),
            "mean_duration_delta_bootstrap_95_ci_percent": ci,
            "mean_peak_rss_delta_percent": statistics.mean(rss),
            "median_peak_rss_delta_percent": statistics.median(rss),
        }
    all_duration = [r["candidate_vs_baseline_duration_delta_percent"] for r in run["timing_rows"]]
    all_rss = [r["candidate_vs_baseline_peak_rss_delta_percent"] for r in run["timing_rows"]]
    overall_ci = bootstrap_mean_ci(all_duration)
    run["summary"] = {
        "per_clip": summaries,
        "overall": {
            "pairs": len(all_duration),
            "mean_duration_delta_percent": statistics.mean(all_duration),
            "median_duration_delta_percent": statistics.median(all_duration),
            "mean_duration_delta_bootstrap_95_ci_percent": overall_ci,
            "mean_peak_rss_delta_percent": statistics.mean(all_rss),
            "median_peak_rss_delta_percent": statistics.median(all_rss),
        },
        "interpretation": "Measurement only. Native ARM64 results do not establish amd64 performance or a speed win.",
    }
    run["complete"] = True
    atomic_json(result_path, run)
    print(f"Saved timing results: {result_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
