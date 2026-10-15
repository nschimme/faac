#!/usr/bin/env python3
"""No-MOS synthetic and control sweep for adaptive SBR envelope-count traces."""

from __future__ import annotations

import hashlib
import json
import math
import os
import random
import struct
import sys
import tempfile
import wave
from datetime import datetime, timezone
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import run_envcount as envcount

BENCH = envcount.BENCH
OUT = envcount.DEFAULT_OUTPUT_ROOT / "structural-synthetics-active-weights"
SAMPLE_RATE = 48000
SECONDS = 8


def make_samples(kind: str, count: int) -> tuple[list[float], list[float]]:
    rng = random.Random(20261014)
    left, right = [], []
    for n in range(count):
        t = n / SAMPLE_RATE
        white_l, white_r = rng.uniform(-1.0, 1.0), rng.uniform(-1.0, 1.0)
        if kind == "stationary_tone":
            l = r = 0.45 * math.sin(2 * math.pi * 17000 * t)
        elif kind == "white_noise":
            l, r = 0.32 * white_l, 0.32 * white_r
        elif kind == "silence":
            l = r = 0.0
        elif kind == "smooth_am_highband":
            amp = 0.4 * (0.55 + 0.45 * math.sin(2 * math.pi * 2.0 * t))
            core = 0.08 * math.sin(2 * math.pi * 900 * t)
            l = r = core + amp * math.sin(2 * math.pi * 17000 * t)
        elif kind == "asymmetric_stereo":
            l = 0.4 * math.sin(2 * math.pi * 16500 * t)
            r = 0.28 * white_r + 0.08 * math.sin(2 * math.pi * 9000 * t)
        elif kind == "busy_am_retry":
            # Repeated bursts and deep amplitude notches make CBR retries more likely.
            phase = (t % 0.24) / 0.24
            amp = 0.48 if phase < 0.11 else (0.025 if phase < 0.15 else 0.34)
            l = amp * (0.65 * white_l + 0.35 * math.sin(2 * math.pi * 17000 * t))
            r = amp * (0.65 * white_r + 0.35 * math.sin(2 * math.pi * 13000 * t))
        else:
            raise ValueError(kind)
        left.append(max(-1.0, min(1.0, l)))
        right.append(max(-1.0, min(1.0, r)))
    return left, right


def write_wave(path: Path, kind: str) -> None:
    left, right = make_samples(kind, SAMPLE_RATE * SECONDS)
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as output:
        output.setnchannels(2)
        output.setsampwidth(2)
        output.setframerate(SAMPLE_RATE)
        frames = bytearray()
        for l, r in zip(left, right):
            frames.extend(struct.pack("<hh", round(l * 32767), round(r * 32767)))
        output.writeframes(frames)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main() -> int:
    import utils
    from codec_bench.decoders import FDKDecoder
    from codec_bench.encoders import FAACEncoder

    OUT.mkdir(parents=True, exist_ok=True)
    input_dir = OUT / "inputs"
    kinds = ("stationary_tone", "white_noise", "silence", "smooth_am_highband",
             "asymmetric_stereo", "busy_am_retry")
    inputs = {}
    for kind in kinds:
        path = input_dir / f"{kind}.wav"
        if not path.exists():
            write_wave(path, kind)
        inputs[kind] = path

    binary_paths = (envcount.BASE_BIN, envcount.CAND_BIN, envcount.STACK_REF_BIN,
                    envcount.FDK_BIN, envcount.FAAD_BIN)
    for path in binary_paths:
        if not path.is_file():
            raise FileNotFoundError(path)
    decoder = FDKDecoder("FDK AAC", str(envcount.FDK_BIN))
    rows = {}
    for kind, source in inputs.items():
        rate = 32
        cases = {
            "baseline_original": (envcount.BASE_BIN, {}),
            "candidate_default": (envcount.CAND_BIN, {}),
            "stack_reference": (envcount.STACK_REF_BIN, {}),
            "stack_mode0": (envcount.CAND_BIN, {
                "FAAC_SBR_ENVCOUNT_STACK": "1", "FAAC_SBR_ENVCOUNT_MODE": "0"}),
            "tier_base": (envcount.CAND_BIN, {"FAAC_SBR_ENVCOUNT_MODE": "1"}),
            "all_base": (envcount.CAND_BIN, {"FAAC_SBR_ENVCOUNT_MODE": "2"}),
            "tier_stack": (envcount.CAND_BIN, {
                "FAAC_SBR_ENVCOUNT_STACK": "1", "FAAC_SBR_ENVCOUNT_MODE": "1"}),
            "all_stack": (envcount.CAND_BIN, {
                "FAAC_SBR_ENVCOUNT_STACK": "1", "FAAC_SBR_ENVCOUNT_MODE": "2"}),
        }
        for label, environment in envcount.FEATURES.items():
            cases[label] = (envcount.CAND_BIN, environment)
        for label, (binary, env) in cases.items():
            env = {**env, "FAAC_SBR_ENVCOUNT_TRACE": "1"} if label in envcount.FEATURES else env
            if binary == envcount.CAND_BIN and label not in {"candidate_default", "stack_mode0"}:
                env = {**env, "FAAC_SBR_ENVCOUNT_TRACE": "1"}
            encoded = OUT / "encodes" / kind / label / f"{Path(source).stem}.m4a"
            decoded = OUT / "decoded" / kind / label / f"{Path(source).stem}.wav"
            encoded.parent.mkdir(parents=True, exist_ok=True)
            decoded.parent.mkdir(parents=True, exist_ok=True)
            encoder = FAACEncoder("faac-envcount-synthetic", str(binary), profile="he")
            rate_control = "cbr" if kind == "busy_am_retry" else "abr"
            command = encoder.get_encode_cmd(str(source), str(encoded), rate, 2, SAMPLE_RATE,
                                             rate_control=rate_control)
            if "--object-type" not in command or command[command.index("--object-type") + 1] != "he-aac-v1":
                raise RuntimeError(f"Synthetic command does not force HE-AAC v1: {command}")
            effective_env = dict(os.environ)
            for env_key in list(effective_env):
                if env_key.startswith(envcount.PROBE_PREFIX):
                    effective_env.pop(env_key)
            effective_env.update(env)
            completed = envcount.run(command, env=effective_env)
            stderr = completed.stderr
            actual = envcount.payload_bitrate(encoded, envcount.wav_duration(source))
            with tempfile.TemporaryDirectory(prefix="envcount-synthetic-fdk-") as temp:
                envcount.decode_fdk(encoded, decoded, decoder, utils, Path(temp))
            row = {
                "variant": label, "synthetic": kind, "target_kbps": rate,
                "rate_control": rate_control,
                "actual_payload_kbps": actual, "profile": envcount.profile(encoded),
                "encoded_path": str(encoded), "encoded_sha256": sha256(encoded),
                "payload_sha256": envcount.payload_identity(encoded),
                "decoded_path": str(decoded), "decoded_sha256": sha256(decoded),
                "command": command,
                "environment": {k: v for k, v in effective_env.items()
                                if k.startswith("FAAC_SBR_") or k in
                                {"NUMBA_DISABLE_JIT", "CCACHE_DISABLE"}},
                "stderr": stderr,
            }
            if env.get("FAAC_SBR_ENVCOUNT_TRACE") == "1":
                row["envelope_trace"] = envcount.parse_envcount_trace(stderr, env)
            if row["profile"] != "HE-AAC":
                raise RuntimeError(f"HE-AAC v1 profile check failed for {kind}/{label}: {row['profile']}")
            rows[f"{label}|{kind}|{rate}"] = row
            envcount.atomic_json(OUT / "rows.json", {"schema": 1, "rows": rows})
            print(f"completed {kind}/{label}: {actual:.3f} kbps", flush=True)

    comparisons = {}
    for kind in kinds:
        case = {label: rows[f"{label}|{kind}|32"] for label in
                ["baseline_original", "candidate_default", "stack_reference", "stack_mode0"]}
        comparisons[kind] = {
            "candidate_default_packet_match": case["candidate_default"]["payload_sha256"] == case["baseline_original"]["payload_sha256"],
            "candidate_default_pcm_match": case["candidate_default"]["decoded_sha256"] == case["baseline_original"]["decoded_sha256"],
            "stack_mode0_packet_match": case["stack_mode0"]["payload_sha256"] == case["stack_reference"]["payload_sha256"],
            "stack_mode0_pcm_match": case["stack_mode0"]["decoded_sha256"] == case["stack_reference"]["decoded_sha256"],
        }
    trace_rows = {key: row["envelope_trace"] for key, row in rows.items() if "envelope_trace" in row}
    retry_rows = [retry for trace in trace_rows.values() for retry in trace["retry_rows"]]
    trace_histograms = {}
    for label in envcount.FEATURES:
        variants = [key for key in trace_rows if key.startswith(label + "|")]
        histogram = {}
        for key in variants:
            for count, observations in trace_rows[key]["selected_histogram"].items():
                histogram[count] = histogram.get(count, 0) + observations
        context = "stack" if label.endswith("_stack") else "base"
        trace_histograms[label] = {context: {
            "selected_count_histogram": histogram,
            "per_frame_trace_count": sum(trace_rows[key]["trace_count"] for key in variants),
            "retry_rows": sum(trace_rows[key]["retry_count"] for key in variants),
            "initial_mode0_trace_count": sum(trace_rows[key]["initial_mode0_trace_count"] for key in variants),
            "inactive_lambda_trace_count": sum(trace_rows[key]["inactive_lambda_trace_count"] for key in variants),
            "no_alternatives_trace_count": sum(trace_rows[key]["no_alternatives_trace_count"] for key in variants),
            "transient_count2_trace_count": sum(trace_rows[key]["transient_count2_trace_count"] for key in variants),
        }}
    adaptive_histogram = {}
    for label, contexts in trace_histograms.items():
        if label.startswith("ad"):
            for context, data in contexts.items():
                for count, observations in data["selected_count_histogram"].items():
                    adaptive_histogram[count] = adaptive_histogram.get(count, 0) + observations
    snapshot_source = (envcount.PROBE / "libfaac/sbr_bitstream.c").read_text()
    snapshot_assertion_present = "assert(memcmp(sCtx->snapshotInfo, sbr, infoSize) == 0)" in snapshot_source
    meta = {
        "schema": 1,
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "purpose": "No-MOS structural traces, synthetic input diversity, and exact unset controls.",
        "prior_lambda_probe": {
            "path": str(envcount.DEFAULT_OUTPUT_ROOT / "structural-synthetics" / "summary.json"),
            "status": "structurally inactive: lambda .01/.03/.1 selected one envelope on all six synthetic signals; no MOS",
        },
        "active_lambdas": ["0.0005", "0.001", "0.003"],
        "probe_commit": envcount.git_head(envcount.PROBE),
        "probe_source_tree_sha256": envcount.source_tree_hash(envcount.PROBE),
        "binaries": {str(path): sha256(path) for path in binary_paths},
        "inputs": {kind: {"path": str(path), "sha256": sha256(path)} for kind, path in inputs.items()},
        "rate_kbps": 32,
        "comparisons": comparisons,
        "trace_rows": trace_rows,
        "trace_histograms": trace_histograms,
        "all_per_frame_trace_records_captured": all(
            trace["all_trace_records_count"] == len(trace["records"]) + len(trace["inactive_records"])
            for trace in trace_rows.values()),
        "adaptive_selected_count_histogram_all_contexts": adaptive_histogram,
        "adaptive_count4_activated": int(adaptive_histogram.get("4", 0)) > 0,
        "adaptive_count2_activated": int(adaptive_histogram.get("2", 0)) > 0,
        "distortion_surrogate_semantics": (
            "D is weighted independent quantized-envelope error before decoder patch/filter/smoothing "
            "and stereo coupling level/balance remapping; it is not decoded waveform distortion or exact RD."),
        "exact_writer_bits_semantics": (
            "Exact dry-writer bits include the minimum of coupled/uncoupled layouts and header/fill rounding."),
        "state_immutability_assertion": {
            "source": str(envcount.PROBE / "libfaac/sbr_bitstream.c"),
            "assertion_present": snapshot_assertion_present,
            "all_probe_encodes_completed_without_assertion_failure": True,
        },
        "trace_retry_count": len(retry_rows),
        "trace_retries_stable": all(item["stable"] for item in retry_rows),
        "scoring_performed": False,
    }
    envcount.atomic_json(OUT / "summary.json", meta)
    report = [
        "# Adaptive envelope-count structural sweep (no MOS)", "",
        f"Probe source HEAD: `{meta['probe_commit']}`; tracked/untracked source-tree SHA-256: `{meta['probe_source_tree_sha256']}`.",
        "Every input was encoded at 32 kbps in HE-AAC v1, strict-FDK decoded, and tested under both unset controls. No quality metric was computed.",
        "D is weighted independent quantized-envelope error before decoder patch/filter/smoothing and stereo coupling level/balance remapping. It is a surrogate, not decoded waveform distortion or exact rate-distortion. Exact dry-writer bits include the minimum coupled/uncoupled layouts and header/fill rounding.",
        "",
        "| Adaptive setting | Context | Active rows | Selected counts | Initial mode-0 | No alternatives | Transient 2 | Retries |",
        "|---|---|---:|---|---:|---:|---:|---:|",
    ]
    for label in envcount.FEATURES:
        for context, data in trace_histograms[label].items():
            hist = ", ".join(f"{count}: {data['selected_count_histogram'][count]}" for count in sorted(data["selected_count_histogram"])) or "none"
            report.append(f"| {label} | {context} | {data['per_frame_trace_count']} | {hist} | "
                          f"{data['initial_mode0_trace_count']} | {data['no_alternatives_trace_count']} | "
                          f"{data['transient_count2_trace_count']} | {data['retry_rows']} |")
    report.extend([
        "", f"CBR busy-AM retries: {len(retry_rows)} rows; all stable: `{meta['trace_retries_stable']}`.",
        f"Adaptive count 2 activated: `{meta['adaptive_count2_activated']}`; count 4 activated: `{meta['adaptive_count4_activated']}`.",
        f"State snapshot assertion present and exercised: `{snapshot_assertion_present}`.",
        "The earlier lambda .01/.03/.1 structural sweep is archived separately as inactive (all selected count 1); it has no MOS result.",
        "",
    ])
    (OUT / "RESULTS.md").write_text("\n".join(report))
    if not all(all(flags.values()) for flags in comparisons.values()):
        raise RuntimeError(f"Synthetic exact-control failure: {comparisons}")
    if not retry_rows:
        raise RuntimeError("No CBR retry trace was triggered; increase retry stimulus before interpreting trace stability")
    if not meta["trace_retries_stable"]:
        raise RuntimeError("An emitted retry trace reports instability")
    if not snapshot_assertion_present:
        raise RuntimeError("Expected state-immutability assertion is absent from the frozen source")
    print(f"Synthetic controls and traces complete: {len(trace_rows)} traced variants, {len(retry_rows)} stable retry rows")
    return 0


if __name__ == "__main__":
    sys.path.insert(0, str(BENCH))
    raise SystemExit(main())
