#!/usr/bin/env python3
"""Matched native encoder timing: 20 AB/BA pairs on each fixed 120 s WAV.

Run only when the host is otherwise idle. The cropped PCM inputs, binaries,
libraries, commands, environment, raw times, and paired summaries are recorded
in /private/tmp/faac-upstream098-native-timing/results.json.
"""
from __future__ import annotations

import hashlib
import json
import os
import platform
import random
import statistics
import subprocess
import time
import wave
from pathlib import Path

BENCH_AUDIO = Path("/Users/nschimme/gitprojects/faac-benchmark/data/external/throughput")
ROOT = Path("/private/tmp/faac-upstream098-native-timing")
INPUTS = ROOT / "inputs"
OUTPUTS = ROOT / "encodes"
BASE_REPO = Path("/private/tmp/faac-upstream098-baseline")
CAND_REPO = Path("/private/tmp/faac-upstream098-fixed8")
BASE = BASE_REPO / "build-release/frontend/faac"
CAND = CAND_REPO / "build-release/frontend/faac"
CLIPS = ("music_percussive.wav", "music_tonal.wav")
RATE = 48
PAIRS = 20
DURATION = 120


def sha(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def crop_wav(source: Path, target: Path) -> dict:
    with wave.open(str(source), "rb") as src:
        params = src.getparams()
        count = params.framerate * DURATION
        pcm = src.readframes(count)
        if len(pcm) != count * params.nchannels * params.sampwidth:
            raise RuntimeError(f"{source} is shorter than {DURATION} seconds")
        with wave.open(str(target), "wb") as dst:
            dst.setparams(params)
            dst.writeframes(pcm)
    return {
        "source_path": str(source), "source_sha256": sha(source),
        "source_seconds": params.nframes / params.framerate,
        "source_frames": params.nframes,
        "source_rate": params.framerate, "source_channels": params.nchannels,
        "input_path": str(target), "input_sha256": sha(target),
        "input_pcm_sha256": hashlib.sha256(pcm).hexdigest(),
        "input_frames": count, "input_seconds": DURATION,
        "sample_width": params.sampwidth,
    }


def run_one(label: str, binary: Path, clip: str, pair: int, order: int,
            env: dict) -> dict:
    source = INPUTS / clip
    output = OUTPUTS / clip / f"{pair:02d}-{order}-{label}.m4a"
    output.parent.mkdir(parents=True, exist_ok=True)
    command = [str(binary), "-b", str(RATE), "--object-type", "he-aac-v1",
               "--overwrite", "-o", str(output), str(source)]
    start = time.perf_counter_ns()
    completed = subprocess.run(command, cwd=str(BASE_REPO), env=env,
                               capture_output=True, text=True)
    elapsed = (time.perf_counter_ns() - start) / 1e9
    if completed.returncode or not output.is_file():
        raise RuntimeError(f"{label} failed for {clip}/{pair}/{order}: "
                           f"{completed.returncode}: {completed.stderr[-1000:]}")
    result = {
        "variant": label, "clip": clip, "pair": pair, "order": order,
        "binary": str(binary), "command": command, "elapsed_s": elapsed,
        "returncode": completed.returncode,
        "encoded_path": str(output), "encoded_sha256": sha(output),
        "stderr_tail": completed.stderr[-300:],
    }
    output.unlink()
    return result


def bootstrap_ci(values: list[float]) -> tuple[float, float]:
    rng = random.Random(980148)
    samples = []
    for _ in range(20000):
        picked = [values[rng.randrange(len(values))] for _ in values]
        samples.append(statistics.median(picked))
    samples.sort()
    return samples[int(0.025 * len(samples))], samples[int(0.975 * len(samples))]


def main() -> int:
    ROOT.mkdir(parents=True, exist_ok=True)
    INPUTS.mkdir(parents=True, exist_ok=True)
    OUTPUTS.mkdir(parents=True, exist_ok=True)
    inputs = {name: crop_wav(BENCH_AUDIO / name, INPUTS / name) for name in CLIPS}
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "LANG": "C", "LC_ALL": "C"}
    variants = {
        "baseline": {"binary": str(BASE), "binary_sha256": sha(BASE),
                     "libfaac": str(BASE_REPO / "build-release/libfaac/libfaac.3.dylib"),
                     "libfaac_sha256": sha(BASE_REPO / "build-release/libfaac/libfaac.3.dylib"),
                     "source_commit": "098e8900289b87d1592a859351ca0bd420397d4b"},
        "fixed8": {"binary": str(CAND), "binary_sha256": sha(CAND),
                   "libfaac": str(CAND_REPO / "build-release/libfaac/libfaac.3.dylib"),
                   "libfaac_sha256": sha(CAND_REPO / "build-release/libfaac/libfaac.3.dylib"),
                   "source_commit": "098e8900289b87d1592a859351ca0bd420397d4b",
                   "patch_sha256": sha(Path("/private/tmp/upstream_fixed8.patch"))},
    }
    # Untimed warm-up for both encoders on both inputs.
    warmups = []
    for clip in CLIPS:
        for label, binary in (("baseline", BASE), ("fixed8", CAND)):
            record = run_one(label, binary, clip, -1, 0, env)
            warmups.append({key: value for key, value in record.items()
                            if key not in ("elapsed_s", "encoded_sha256")})

    measurements = []
    for clip in CLIPS:
        for pair in range(PAIRS):
            order = (("baseline", BASE), ("fixed8", CAND)) if pair % 2 == 0 else (("fixed8", CAND), ("baseline", BASE))
            for position, (label, binary) in enumerate(order):
                measurements.append(run_one(label, binary, clip, pair, position, env))
            print(f"finished {clip}: pair {pair + 1}/{PAIRS}", flush=True)

    paired = {}
    for clip in CLIPS:
        values = []
        for pair in range(PAIRS):
            group = [m for m in measurements if m["clip"] == clip and m["pair"] == pair]
            by_label = {m["variant"]: m["elapsed_s"] for m in group}
            values.append(100.0 * (by_label["fixed8"] / by_label["baseline"] - 1.0))
        lo, hi = bootstrap_ci(values)
        paired[clip] = {
            "pairs": len(values), "median_fixed8_vs_baseline_pct": statistics.median(values),
            "bootstrap_95ci_median_pct": [lo, hi],
            "median_baseline_s": statistics.median(m["elapsed_s"] for m in measurements if m["clip"] == clip and m["variant"] == "baseline"),
            "median_fixed8_s": statistics.median(m["elapsed_s"] for m in measurements if m["clip"] == clip and m["variant"] == "fixed8"),
            "paired_pct_deltas": values,
        }
    pooled = [100.0 * (b["elapsed_s"] / a["elapsed_s"] - 1.0)
              for clip in CLIPS for pair in range(PAIRS)
              for a in measurements if a["clip"] == clip and a["pair"] == pair and a["variant"] == "baseline"
              for b in measurements if b["clip"] == clip and b["pair"] == pair and b["variant"] == "fixed8"]
    lo, hi = bootstrap_ci(pooled)
    result = {
        "schema": 1, "host": platform.platform(), "machine": platform.machine(),
        "method": "20 paired runs per clip; alternate AB then BA pair order; subprocess wall-clock; untimed warm-up for both variants per clip",
        "audio_duration_seconds": DURATION, "target_kbps": RATE,
        "encoder_flags": ["-b", str(RATE), "--object-type", "he-aac-v1", "--overwrite"],
        "environment": env, "inherited_FAAC_SBR_variables": {},
        "variants": variants, "inputs": inputs, "warmups": warmups,
        "paired_summary_by_clip": paired,
        "pooled": {"pairs": len(pooled), "median_fixed8_vs_baseline_pct": statistics.median(pooled),
                   "bootstrap_95ci_median_pct": [lo, hi]},
        "measurements": measurements,
    }
    (ROOT / "results.json").write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    print(json.dumps({"paired_summary_by_clip": paired, "pooled": result["pooled"]}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
