#!/usr/bin/env python3
"""Measure HE-AAC v1 SBR envelope-count controls and adaptive candidates.

Run with the faac-benchmark virtualenv. This harness does not modify encoder
sources and does not make a shipping recommendation.
"""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import math
import os
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import threading
import wave
from datetime import datetime, timezone
from pathlib import Path

BENCH = Path("/Users/nschimme/gitprojects/faac-benchmark")
PROBE = Path("/private/tmp/faac-he-v1-envelopes")
BASE_BIN = Path("/private/tmp/faac-he-v1-probes/build-baseline/frontend/faac")
CAND_BIN = PROBE / "build-probe/frontend/faac"
STACK_REF_BIN = Path("/private/tmp/faac-he-v1-candidate/build-candidate/frontend/faac")
FDK_BIN = BENCH / "bin/fdkdec"
FAAD_BIN = Path("/private/tmp/faac-he-v1-decoder-build/frontend/faad")
FFMPEG = shutil.which("ffmpeg")
BASE_RUN_ROOT = Path("/private/tmp/faac-he-v1-probes/probe")
STACK_RUN_ROOT = Path("/Users/nschimme/gitprojects/faac/.cache/he-v1-feature-study/stack")
DEFAULT_OUTPUT_ROOT = Path("/Users/nschimme/gitprojects/faac/.cache/he-v1-feature-study/envelope-count")
GATE_CLIPS = ["sandman.16b48k.wav", "velvet.16b48k.wav",
              "21-classic.441.16b48k.wav", "fms.wav"]
RATES = [32, 48, 64, 96]
PROBE_PREFIX = "FAAC_SBR_"
STACK_CONTROL_KEY = "stack18"

# Fixed modes have no lambda parameter. Adaptive-mode lambda values are
# independent sweep points, repeated in baseline-only and retained-stack contexts.
FEATURES = {
    "tier_base": {"FAAC_SBR_ENVCOUNT_MODE": "1"},
    "all_base": {"FAAC_SBR_ENVCOUNT_MODE": "2"},
    "ad0005_base": {"FAAC_SBR_ENVCOUNT_MODE": "3", "FAAC_SBR_ENVCOUNT_LAMBDA": "0.0005"},
    "ad001_base": {"FAAC_SBR_ENVCOUNT_MODE": "3", "FAAC_SBR_ENVCOUNT_LAMBDA": "0.001"},
    "ad003_base": {"FAAC_SBR_ENVCOUNT_MODE": "3", "FAAC_SBR_ENVCOUNT_LAMBDA": "0.003"},
    "tier_stack": {"FAAC_SBR_ENVCOUNT_STACK": "1", "FAAC_SBR_ENVCOUNT_MODE": "1"},
    "all_stack": {"FAAC_SBR_ENVCOUNT_STACK": "1", "FAAC_SBR_ENVCOUNT_MODE": "2"},
    "ad0005_stack": {"FAAC_SBR_ENVCOUNT_STACK": "1", "FAAC_SBR_ENVCOUNT_MODE": "3", "FAAC_SBR_ENVCOUNT_LAMBDA": "0.0005"},
    "ad001_stack": {"FAAC_SBR_ENVCOUNT_STACK": "1", "FAAC_SBR_ENVCOUNT_MODE": "3", "FAAC_SBR_ENVCOUNT_LAMBDA": "0.001"},
    "ad003_stack": {"FAAC_SBR_ENVCOUNT_STACK": "1", "FAAC_SBR_ENVCOUNT_MODE": "3", "FAAC_SBR_ENVCOUNT_LAMBDA": "0.003"},
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def source_tree_hash(path: Path) -> str:
    tracked = subprocess.check_output(["git", "-C", str(path), "ls-files", "-z"]).decode().split("\0")
    untracked = subprocess.check_output([
        "git", "-C", str(path), "ls-files", "--others", "--exclude-standard", "-z"
    ]).decode().split("\0")
    digest = hashlib.sha256()
    for name in sorted({item for item in tracked + untracked if item}):
        path_item = path / name
        if path_item.is_file():
            digest.update(name.encode("utf-8", "surrogateescape") + b"\0")
            digest.update(path_item.read_bytes() + b"\0")
    return digest.hexdigest()


def source_diff_hash(path: Path) -> str:
    try:
        subprocess.check_output(["git", "-C", str(path), "rev-parse", "--verify", "upstream/master"],
                                stderr=subprocess.DEVNULL)
        committed = subprocess.check_output(["git", "-C", str(path), "diff", "--binary", "upstream/master...HEAD"])
    except subprocess.CalledProcessError:
        committed = subprocess.check_output(["git", "-C", str(path), "diff", "--binary", "HEAD"])
    staged = subprocess.check_output(["git", "-C", str(path), "diff", "--cached", "--binary"])
    working = subprocess.check_output(["git", "-C", str(path), "diff", "--binary"])
    return hashlib.sha256(committed + b"\0" + staged + b"\0" + working).hexdigest()


def git_head(path: Path) -> str:
    return subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()


def linked_libraries(binary: Path) -> list[dict]:
    if not shutil.which("otool"):
        return []
    try:
        output = subprocess.check_output(["otool", "-L", str(binary)], text=True, stderr=subprocess.DEVNULL)
    except (OSError, subprocess.CalledProcessError):
        return []
    items = []
    for line in output.splitlines()[1:]:
        name, _, version = line.strip().partition(" (compatibility version")
        resolved = None
        if name == "@rpath/libfaac.2.dylib":
            if binary == BASE_BIN:
                resolved = BASE_BIN.parents[1] / "libfaac/libfaac.2.dylib"
            elif binary == STACK_REF_BIN:
                resolved = STACK_REF_BIN.parents[1] / "libfaac/libfaac.2.dylib"
            elif binary == CAND_BIN:
                resolved = CAND_BIN.parents[1] / "libfaac/libfaac.2.dylib"
        items.append({"install_name": name, "resolved_path": str(resolved) if resolved else None,
                      "sha256": sha256(resolved) if resolved and resolved.is_file() else None,
                      "version_info": version})
    return items


def atomic_json(path: Path, data) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n")
    os.replace(tmp, path)


def run(cmd, *, env=None):
    return subprocess.run(cmd, check=True, text=True, capture_output=True, env=env)


def payload_identity(path: Path) -> str:
    result = run(["ffprobe", "-v", "error", "-select_streams", "a:0", "-show_packets",
                  "-show_data_hash", "sha256", "-show_entries", "packet=size,data_hash",
                  "-of", "json", str(path)])
    packets = json.loads(result.stdout).get("packets", [])
    if not packets:
        raise RuntimeError(f"ffprobe found no AAC packets: {path}")
    digest = hashlib.sha256()
    for packet in packets:
        if "size" not in packet or "data_hash" not in packet:
            raise RuntimeError(f"ffprobe omitted packet identity fields: {path}")
        digest.update(str(int(packet["size"])).encode("ascii") + b":" + packet["data_hash"].encode("ascii") + b"\n")
    return digest.hexdigest()


def payload_bitrate(path: Path, duration: float) -> float:
    result = run(["ffprobe", "-v", "error", "-select_streams", "a:0", "-show_packets",
                  "-show_entries", "packet=size", "-of", "json", str(path)])
    sizes = [int(packet["size"]) for packet in json.loads(result.stdout).get("packets", []) if "size" in packet]
    if not sizes or duration <= 0:
        raise RuntimeError(f"Could not calculate packet payload bitrate: {path}")
    return sum(sizes) * 8.0 / duration / 1000.0


def profile(path: Path) -> str:
    result = run(["ffprobe", "-v", "error", "-select_streams", "a:0", "-show_entries",
                  "stream=profile", "-of", "json", str(path)])
    streams = json.loads(result.stdout).get("streams", [])
    if len(streams) != 1:
        raise RuntimeError(f"Expected one audio stream in {path}")
    return streams[0].get("profile", "")


def wav_duration(path: Path) -> float:
    with wave.open(str(path), "rb") as reader:
        return reader.getnframes() / reader.getframerate()


def encode(binary, source, output, rate, env_values, FAACEncoder):
    encoder = FAACEncoder("faac-envcount-probe", str(binary), profile="he")
    command = encoder.get_encode_cmd(str(source), str(output), rate, 2, 48000)
    if "--object-type" not in command or command[command.index("--object-type") + 1] != "he-aac-v1":
        raise RuntimeError(f"Command does not force HE-AAC v1: {command}")
    env = dict(os.environ)
    for key in list(env):
        if key.startswith(PROBE_PREFIX):
            env.pop(key)
    env.update(env_values)
    result = run(command, env=env)
    if profile(output) != "HE-AAC":
        raise RuntimeError(f"Expected HE-AAC profile from {output}")
    stderr = result.stderr if env_values.get("FAAC_SBR_ENVCOUNT_TRACE") == "1" else result.stderr[-4000:]
    return command, env_values, stderr, payload_bitrate(output, wav_duration(source))


def parse_envcount_trace(stderr: str, expected_environment=None) -> dict:
    headers = []
    for line in stderr.splitlines():
        if "ENVCOUNT_TRACE " not in line:
            continue
        headers.append(line[line.index("ENVCOUNT_TRACE "):])
    if not headers:
        raise RuntimeError("Trace-enabled encode emitted no ENVCOUNT_TRACE header")
    records = []
    for header in headers:
        values = dict(re.findall(r"([a-z_][a-z_0-9]*)=([^ ]+)", header))
        if not {"mode", "stack", "lambda", "candidates", "selected"}.issubset(values):
            raise RuntimeError(f"Malformed ENVCOUNT_TRACE header: {header}")
        candidates = []
        for index in range(int(values["candidates"])):
            token = values.get(f"c{index}")
            if token is None:
                raise RuntimeError(f"Missing candidate c{index}: {header}")
            count, distortion, exact_bits, objective = token.split(",")
            distortion, objective, exact_bits = float(distortion), float(objective), int(exact_bits)
            if not math.isfinite(distortion) or not math.isfinite(objective) or exact_bits < 0:
                raise RuntimeError(f"Non-finite/invalid D, J or exact bits in trace: {header}")
            candidates.append({"envelopes": int(count), "distortion": float(distortion),
                               "exact_bits": exact_bits, "objective": objective})
        selected = int(values["selected"])
        if selected not in {candidate["envelopes"] for candidate in candidates}:
            raise RuntimeError(f"Invalid selected count in trace: {header}")
        records.append({"header": header, "mode": int(values["mode"]), "stack": int(values["stack"]),
                        "lambda": values["lambda"], "candidates": candidates,
                        "selected": selected})
    retries = []
    for line in stderr.splitlines():
        if not line.startswith("ENVCOUNT_RETRY "):
            continue
        fields = dict(re.findall(r"([a-z_][a-z_0-9]*)=([^ ]+)", line))
        retries.append({"frame": int(fields["frame"]), "retry": int(fields["retry"]),
                        "selected": int(fields["selected"]), "stable": fields["stable"] == "1"})
    if any(not retry["stable"] for retry in retries):
        raise RuntimeError("Trace reports an unstable retry selection")
    expected_environment = expected_environment or {}
    expected_mode = int(expected_environment.get("FAAC_SBR_ENVCOUNT_MODE", "0"))
    expected_stack = int(expected_environment.get("FAAC_SBR_ENVCOUNT_STACK", "0"))
    expected_lambda = expected_environment.get(
        "FAAC_SBR_ENVCOUNT_LAMBDA", "0.03" if expected_mode else "0")
    mode_stack_records = [record for record in records if record["mode"] == expected_mode
                          and record["stack"] == expected_stack]
    if expected_mode == 3:
        active_records = [record for record in mode_stack_records if record["lambda"] == expected_lambda]
        inactive_lambda_records = [record for record in mode_stack_records if record["lambda"] != expected_lambda]
    else:
        active_records = mode_stack_records
        inactive_lambda_records = []
    if not active_records:
        raise RuntimeError(f"Trace contains no records for expected environment {expected_environment}")
    active_ids = {id(record) for record in active_records}
    inactive_records = [record for record in records if id(record) not in active_ids]
    selected_histogram = {}
    for record in active_records:
        key = str(record["selected"])
        selected_histogram[key] = selected_histogram.get(key, 0) + 1
    return {"header": active_records[0]["header"], "mode": active_records[0]["mode"],
            "stack": active_records[0]["stack"], "lambda": active_records[0]["lambda"],
            "candidates": active_records[0]["candidates"], "selected": active_records[0]["selected"],
            "records": active_records, "trace_count": len(active_records),
            "all_trace_records_count": len(records),
            "inactive_records": inactive_records,
            "initial_mode0_trace_count": sum(record["mode"] == 0 for record in records),
            "inactive_lambda_trace_count": len(inactive_lambda_records),
            "no_alternatives_trace_count": sum(len(record["candidates"]) == 1 for record in active_records),
            "transient_count2_trace_count": sum(record["selected"] == 2 for record in active_records),
            "selected_histogram": selected_histogram, "retry_rows": retries,
            "retry_count": len(retries), "all_retries_stable": all(retry["stable"] for retry in retries)}


def decode_fdk(encoded: Path, output: Path, decoder, utils, temp_dir: Path) -> None:
    decoded = temp_dir / (output.stem + ".fdk.wav")
    env = dict(os.environ)
    for key in list(env):
        if key.startswith(PROBE_PREFIX):
            env.pop(key)
    env.update(decoder.get_run_env())
    run(decoder.get_decode_cmd(str(encoded), str(decoded)), env=env)
    if not decoded.is_file() or decoded.stat().st_size == 0:
        raise RuntimeError(f"Strict FDK decode produced no output: {encoded}")
    if not utils.wav_conv(str(decoded), str(output), rate=48000, channels=2):
        raise RuntimeError(f"Could not convert FDK output to 48 kHz stereo: {decoded}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--phase", choices=("gate", "full"), default="gate")
    parser.add_argument("--variants", default="all", help="comma-separated FEATURE names or all")
    parser.add_argument("--rates", default="32,48,64,96")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--output-dir", type=Path, help="defaults to envelope-count/<phase>")
    parser.add_argument("--probe-variety", action="store_true",
                        help="quiet, one-clip mode/profile/FDK control sweep; no MOS scoring")
    parser.add_argument("--controls-only", action="store_true",
                        help="run all gate baseline/default and stack-mode0 exact controls; no MOS scoring")
    parser.add_argument("--prepare-only", action="store_true", help="write provenance config without encoding")
    args = parser.parse_args()
    if args.jobs <= 0:
        parser.error("--jobs must be positive")
    if args.controls_only and (args.probe_variety or args.phase != "gate"):
        parser.error("--controls-only requires --phase gate and cannot be combined with --probe-variety")
    rates = [int(item) for item in args.rates.split(",") if item]
    if not rates or any(rate not in RATES for rate in rates):
        parser.error("rates must be selected from 32,48,64,96 kbps")
    selected = list(FEATURES) if args.variants == "all" else [item.strip() for item in args.variants.split(",") if item.strip()]
    unknown = sorted(set(selected) - set(FEATURES))
    if unknown:
        parser.error(f"Unknown variants {unknown}; choices: {','.join(FEATURES)}")
    if args.phase == "gate" and set(selected) != set(FEATURES):
        raise RuntimeError("The initial gate must include all ten baseline/stack settings")

    os.environ.setdefault("NUMBA_DISABLE_JIT", "1")
    sys.path.insert(0, str(BENCH))
    sys.path.insert(0, str(PROBE / "probe"))
    from codec_bench.encoders import FAACEncoder
    from codec_bench.decoders import FDKDecoder
    from config import _MUSIC_GATE
    from compare_results import bootstrap_mean_ci
    import utils
    from phase2_mos import score_wav_pair
    sys.path.insert(0, str(BENCH / "scripts"))
    from bd_rate import bd_rate_curve

    if args.probe_variety:
        clips = [GATE_CLIPS[0]]
        rates = [64]
    elif args.phase == "gate":
        clips = list(_MUSIC_GATE)
        if clips != GATE_CLIPS:
            raise RuntimeError(f"Benchmark gate clip list changed: {clips}")
    else:
        clips = sorted(path.name for path in (BENCH / "data/external/audio").glob("*.wav"))
        if len(clips) != 49 or len(rates) != 4:
            raise RuntimeError(f"Expected 49 clips x 4 rates; got {len(clips)} x {len(rates)}")

    out_dir = (args.output_dir.expanduser().resolve() if args.output_dir else
               DEFAULT_OUTPUT_ROOT / ("probe-variety" if args.probe_variety else
                                      "controls" if args.controls_only else args.phase))
    out_dir.mkdir(parents=True, exist_ok=True)
    prefix = "variety" if args.probe_variety else "envcount"
    result_path = out_dir / f"{prefix}-results.json"
    config_path = out_dir / f"{prefix}-config.json"
    summary_path = out_dir / f"{prefix}-summary.json"
    summary_md_path = out_dir / "RESULTS.md"

    for binary in (BASE_BIN, CAND_BIN, STACK_REF_BIN, FDK_BIN, FAAD_BIN):
        if not binary.is_file():
            raise FileNotFoundError(binary)
    if not FFMPEG:
        raise FileNotFoundError("ffmpeg is required for post-score decoder validation")

    baseline_phase_dir = "results-gate" if args.phase == "gate" else "results-all"
    old_base_results_path = BASE_RUN_ROOT / baseline_phase_dir / "feature-results.json"
    stack_results_path = STACK_RUN_ROOT / "results-all/stack-results.json"
    old_result_paths = {"baseline": old_base_results_path, "stack": stack_results_path}
    for path in old_result_paths.values():
        if not path.is_file():
            raise FileNotFoundError(path)
    source_dir = BENCH / "data/external/audio"
    clip_hashes = {clip: sha256(source_dir / clip) for clip in clips}
    library_paths = [
        BASE_BIN.parents[1] / "libfaac/libfaac.2.dylib",
        CAND_BIN.parents[1] / "libfaac/libfaac.2.dylib",
        STACK_REF_BIN.parents[1] / "libfaac/libfaac.2.dylib",
    ]
    identity = {
        "runner_script_sha256": sha256(Path(__file__).resolve()),
        "benchmark_commit": git_head(BENCH),
        "probe_commit": git_head(PROBE),
        "source_tree_sha256": source_tree_hash(PROBE),
        "source_diff_sha256": source_diff_hash(PROBE),
        "binaries": {str(binary): sha256(binary) for binary in (BASE_BIN, CAND_BIN, STACK_REF_BIN, FDK_BIN, FAAD_BIN)},
        "linked_libraries": {str(binary): linked_libraries(binary) for binary in (BASE_BIN, CAND_BIN, STACK_REF_BIN)},
        "library_hashes": {str(path): sha256(path) for path in library_paths if path.is_file()},
        "clip_sha256": clip_hashes,
        "clips": clips,
        "rates_kbps": rates,
        "phase": args.phase,
        "variants": selected,
        "feature_environment": FEATURES,
        "baseline_measurements": str(old_base_results_path),
        "stack_measurements": str(stack_results_path),
        "baseline_measurements_sha256": sha256(old_base_results_path),
        "stack_measurements_sha256": sha256(stack_results_path),
        "probe_variety": args.probe_variety,
    }
    config = {
        "schema": 1,
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "immutable_run_config": identity,
        **identity,
        "baseline_binary": str(BASE_BIN),
        "candidate_binary": str(CAND_BIN),
        "retained_stack_reference_binary": str(STACK_REF_BIN),
        "decoder": "FDK AAC, strict no-fallback before MOS; FAAD3 strict and FFmpeg after MOS",
        "metric": "Zimtohrli via phase2_mos.score_wav_pair audio mode at 48 kHz",
        "actual_bitrate": "ffprobe AAC packet bytes * 8 / source WAV duration",
        "workers": args.jobs,
        "scoring_enabled": not (args.probe_variety or args.controls_only),
    }
    old_config = json.loads(config_path.read_text()) if config_path.exists() else None
    if old_config and old_config.get("immutable_run_config") != identity:
        raise RuntimeError("Output directory has a different provenance/configuration; choose a new output directory")
    atomic_json(config_path, config)
    if args.prepare_only:
        print(f"Prepared config only; no encodes started: {config_path}")
        return 0

    old_results = json.loads(result_path.read_text()) if result_path.exists() else {"schema": 1, "rows": {}}
    baseline_reference = json.loads(old_base_results_path.read_text())["rows"]
    stack_reference = json.loads(stack_results_path.read_text())["rows"]
    baseline_names = ("baseline_default",)
    for ref_name in baseline_names:
        missing = [f"{ref_name}|{clip}|{rate}" for clip in clips for rate in rates
                   if f"{ref_name}|{clip}|{rate}" not in baseline_reference]
        if missing:
            raise RuntimeError(f"Baseline prior results lack {ref_name}: {missing[:5]}")
    missing_stack = [f"{STACK_CONTROL_KEY}|{clip}|{rate}" for clip in clips for rate in rates
                     if f"{STACK_CONTROL_KEY}|{clip}|{rate}" not in stack_reference]
    if missing_stack:
        raise RuntimeError(f"Prior stack results lack the retained stack control: {missing_stack[:5]}")

    cache_dir, enc_dir, wav_dir = (out_dir / "cache", out_dir / "encodes", out_dir / "scoring-wav")
    for directory in (cache_dir, enc_dir, wav_dir):
        directory.mkdir(parents=True, exist_ok=True)
    references = {}
    for clip in clips:
        source = source_dir / clip
        with wave.open(str(source), "rb") as reader:
            if reader.getframerate() != 48000 or reader.getnchannels() != 2:
                raise RuntimeError(f"Expected 48 kHz stereo input: {source}")
        reference = cache_dir / f"ref-{clip}"
        if not reference.is_file() and not utils.wav_conv(str(source), str(reference), rate=48000, channels=2):
            raise RuntimeError(f"Cannot prepare scoring reference: {source}")
        references[clip] = reference
    decoder = FDKDecoder("FDK AAC", str(FDK_BIN))
    write_lock = threading.Lock()
    run_rows = old_results["rows"]

    def save_row(key, row):
        with write_lock:
            run_rows[key] = row
            atomic_json(result_path, old_results)

    def ensure_row(label, binary, env_values, clip, rate, *, reuse_source=None, require_match=None,
                   score=True, skip_mos=False):
        key = f"{label}|{clip}|{rate}"
        source = source_dir / clip
        encoded = enc_dir / label / str(rate) / (Path(clip).stem + ".m4a")
        decoded = wav_dir / label / str(rate) / (Path(clip).stem + ".wav")
        encoded.parent.mkdir(parents=True, exist_ok=True)
        decoded.parent.mkdir(parents=True, exist_ok=True)
        cached = run_rows.get(key)
        if (cached and encoded.is_file() and decoded.is_file()
                and cached.get("encoded_sha256") == sha256(encoded)
                and cached.get("decoded_sha256") == sha256(decoded)):
            packet_hash = cached.get("payload_sha256") or payload_identity(encoded)
            actual = cached["actual_payload_kbps"]
            profile_name = cached["profile"]
            command = cached.get("command", [])
            stderr_tail = cached.get("stderr_tail", "")
        else:
            with write_lock:
                run_rows.pop(key, None)
            encoded.unlink(missing_ok=True)
            decoded.unlink(missing_ok=True)
            command, environment, stderr_tail, actual = encode(binary, source, encoded, rate, env_values, FAACEncoder)
            with tempfile.TemporaryDirectory(prefix="envcount-fdk-") as temp:
                decode_fdk(encoded, decoded, decoder, utils, Path(temp))
            packet_hash = payload_identity(encoded)
            profile_name = profile(encoded)
        pcm_hash = sha256(decoded)
        if profile_name != "HE-AAC":
            raise RuntimeError(f"Expected HE-AAC output: {key} profile={profile_name!r}")
        if require_match:
            reference = require_match
            if packet_hash != reference["payload_sha256"] or pcm_hash != reference["decoded_sha256"]:
                raise RuntimeError(f"Exact control mismatch at {key}: {reference['variant']}")
        if skip_mos:
            mos, backend, mos_source = None, None, "not_scored_probe_variety"
            reuse_matched = False
        else:
            mos, backend, mos_source = None, None, None
            reuse_matched = False
            if reuse_source:
                if label.endswith("_base"):
                    source_rows = baseline_reference
                elif label == "baseline_original":
                    source_rows = baseline_reference
                else:
                    source_rows = stack_reference
                prior = source_rows[f"{reuse_source}|{clip}|{rate}"]
                prior_encoded, prior_decoded = Path(prior["encoded_path"]), Path(prior["decoded_path"])
                valid_prior = (prior_encoded.is_file() and prior_decoded.is_file()
                              and prior.get("encoded_sha256") == sha256(prior_encoded)
                              and prior.get("decoded_sha256") == sha256(prior_decoded))
                if valid_prior and packet_hash == payload_identity(prior_encoded) and pcm_hash == sha256(prior_decoded):
                    mos, backend = prior["mos"], prior.get("metric_backend", "zimtohrli")
                    mos_source = f"identity:{reuse_source}|{clip}|{rate}"
                    reuse_matched = True
            if require_match:
                if reference.get("mos") is not None:
                    mos, backend = reference["mos"], reference.get("metric_backend", "zimtohrli")
                    mos_source = "identity:" + reference["variant"]
                    reuse_matched = True
            if score and mos is None:
                mos, backend = score_wav_pair(str(references[clip]), str(decoded), mode_str="audio", sample_rate=48000)
                if mos is None or backend != "zimtohrli" or not math.isfinite(float(mos)):
                    raise RuntimeError(f"Strict Zimtohrli scoring failed: {key}: {mos}, {backend}")
                mos_source = "scored"
        row = {
            "variant": label,
            "clip": clip,
            "target_kbps": rate,
            "actual_payload_kbps": actual,
            "profile": profile_name,
            "mos": float(mos) if mos is not None else None,
            "metric_backend": backend,
            "mos_source": mos_source,
            "reuse_source_matched": reuse_matched,
            "encoded_path": str(encoded),
            "encoded_sha256": sha256(encoded),
            "payload_sha256": packet_hash,
            "decoded_path": str(decoded),
            "decoded_sha256": pcm_hash,
            "environment": env_values,
            "command": command,
            "stderr_tail": stderr_tail,
        }
        if env_values.get("FAAC_SBR_ENVCOUNT_TRACE") == "1":
            row["envelope_trace"] = parse_envcount_trace(stderr_tail, env_values)
        save_row(key, row)
        print(f"completed {key}: payload={actual:.3f} kbps, "
              f"MOS={row['mos'] if row['mos'] is not None else 'not scored'}, source={mos_source}", flush=True)
        return row

    if args.controls_only:
        def run_control_only(clip, rate):
            base = ensure_row("baseline_original", BASE_BIN, {}, clip, rate,
                              reuse_source="baseline_default", score=False, skip_mos=True)
            candidate = ensure_row("candidate_default", CAND_BIN, {}, clip, rate,
                                   require_match=base, score=False, skip_mos=True)
            stack_ref = ensure_row("stack_reference", STACK_REF_BIN, {}, clip, rate,
                                   reuse_source=STACK_CONTROL_KEY, score=False, skip_mos=True)
            stack_zero = ensure_row("stack_mode0", CAND_BIN,
                                    {"FAAC_SBR_ENVCOUNT_STACK": "1", "FAAC_SBR_ENVCOUNT_MODE": "0"},
                                    clip, rate, require_match=stack_ref, score=False, skip_mos=True)
            return {"clip": clip, "rate": rate,
                    "baseline_packet_match": candidate["payload_sha256"] == base["payload_sha256"],
                    "baseline_pcm_match": candidate["decoded_sha256"] == base["decoded_sha256"],
                    "stack_packet_match": stack_zero["payload_sha256"] == stack_ref["payload_sha256"],
                    "stack_pcm_match": stack_zero["decoded_sha256"] == stack_ref["decoded_sha256"]}

        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            controls = list(pool.map(lambda pair: run_control_only(*pair),
                                     [(clip, rate) for clip in clips for rate in rates]))
        atomic_json(out_dir / "controls-summary.json", {
            "schema": 1, "controls": controls, "scoring_performed": False,
            "all_exact": all(all(item[key] for key in ("baseline_packet_match", "baseline_pcm_match",
                                                        "stack_packet_match", "stack_pcm_match"))
                              for item in controls)})
        if len(controls) != 16 or not all(all(item[key] for key in
                ("baseline_packet_match", "baseline_pcm_match", "stack_packet_match", "stack_pcm_match"))
                for item in controls):
            raise RuntimeError("Expected 16 exact gate controls (packet and strict FDK PCM)")
        print(f"Exact gate controls passed: {len(controls)} rows; no MOS scoring performed")
        return 0

    if args.probe_variety:
        clip, rate = clips[0], rates[0]
        base = ensure_row("baseline_original", BASE_BIN, {}, clip, rate,
                          reuse_source="baseline_default", score=False, skip_mos=True)
        candidate_default = ensure_row("candidate_default", CAND_BIN, {}, clip, rate,
                                       require_match=base, score=False, skip_mos=True)
        stack_ref_old = stack_reference[f"{STACK_CONTROL_KEY}|{clip}|{rate}"]
        stack_ref = ensure_row("stack_reference", STACK_REF_BIN, {}, clip, rate,
                               reuse_source=STACK_CONTROL_KEY, score=False, skip_mos=True)
        stack_mode0 = ensure_row("stack_mode0", CAND_BIN,
                                {"FAAC_SBR_ENVCOUNT_STACK": "1", "FAAC_SBR_ENVCOUNT_MODE": "0"},
                                clip, rate, require_match=stack_ref, score=False, skip_mos=True)
        rows = [base, candidate_default, stack_ref, stack_mode0]
        for variant, environment in FEATURES.items():
            rows.append(ensure_row(variant, CAND_BIN,
                                   {**environment, "FAAC_SBR_ENVCOUNT_TRACE": "1"},
                                   clip, rate, score=False, skip_mos=True))
        unique_by_context = {
            "base": len({row["payload_sha256"] for row in rows if row["variant"].endswith("_base")}),
            "stack": len({row["payload_sha256"] for row in rows if row["variant"].endswith("_stack")}),
        }
        variety = {
            "schema": 1,
            "clip": clip,
            "rate_kbps": rate,
            "rows": rows,
            "baseline_control_packet_match": candidate_default["payload_sha256"] == base["payload_sha256"],
            "baseline_control_pcm_match": candidate_default["decoded_sha256"] == base["decoded_sha256"],
            "stack_mode0_packet_match": stack_mode0["payload_sha256"] == stack_ref["payload_sha256"],
            "stack_mode0_pcm_match": stack_mode0["decoded_sha256"] == stack_ref["decoded_sha256"],
            "unique_packets_by_context": unique_by_context,
            "trace_rows": {row["variant"]: row.get("envelope_trace") for row in rows
                           if row.get("envelope_trace") is not None},
            "external_stack_prior_artifact_valid": bool(stack_ref_old.get("payload_sha256")),
            "scoring_performed": False,
        }
        atomic_json(out_dir / "variety-results.json", variety)
        if not all((variety["baseline_control_packet_match"], variety["baseline_control_pcm_match"],
                    variety["stack_mode0_packet_match"], variety["stack_mode0_pcm_match"])):
            raise RuntimeError("Probe-variety exact control failed")
        print(f"Probe variety complete; distinct packets by context: {unique_by_context}")
        return 0

    # Fresh exact baseline and retained-stack controls. Candidate-default must
    # match the original baseline, while candidate stack mode 0 must match the
    # external production stack in both packet and strict FDK PCM identity.
    def run_controls(clip, rate):
        base = ensure_row("baseline_original", BASE_BIN, {}, clip, rate,
                          reuse_source="baseline_default")
        candidate = ensure_row("candidate_default", CAND_BIN, {}, clip, rate, require_match=base)
        stack_ref = ensure_row("stack_reference", STACK_REF_BIN, {}, clip, rate,
                               reuse_source=STACK_CONTROL_KEY)
        stack_zero = ensure_row("stack_mode0", CAND_BIN,
                                {"FAAC_SBR_ENVCOUNT_STACK": "1", "FAAC_SBR_ENVCOUNT_MODE": "0"},
                                clip, rate, require_match=stack_ref)
        return base, candidate, stack_ref, stack_zero

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(run_controls, clip, rate) for clip in clips for rate in rates]
        for future in as_completed(futures):
            future.result()

    def run_variant(variant, clip, rate):
        return ensure_row(variant, CAND_BIN, FEATURES[variant], clip, rate)

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(run_variant, variant, clip, rate)
                   for clip in clips for rate in rates for variant in selected]
        for future in as_completed(futures):
            future.result()

    # Decoder checks are deliberately after all scores, with each unique output
    # checked once by strict in-tree FAAD3 and FFmpeg.
    interop_rows = []
    interop_dir = out_dir / "interop-wav"
    interop_dir.mkdir(parents=True, exist_ok=True)
    for key, row in sorted(run_rows.items()):
        if row.get("variant") not in {"baseline_original", "candidate_default", "stack_reference", "stack_mode0", *selected}:
            continue
        encoded = Path(row["encoded_path"])
        with tempfile.TemporaryDirectory(prefix="envcount-faada-", dir=interop_dir) as temp_dir:
            faad_wav = Path(temp_dir) / f"{Path(encoded).stem}.wav"
            faad = subprocess.run([str(FAAD_BIN), "--strict", "--quiet", "--overwrite", "-o", str(faad_wav), str(encoded)],
                                  capture_output=True, text=True)
            ffmpeg = subprocess.run([FFMPEG, "-v", "error", "-y", "-i", str(encoded), "-f", "null", "-"],
                                    capture_output=True, text=True)
            faad_ok = faad.returncode == 0 and faad_wav.is_file() and faad_wav.stat().st_size > 0
            ffmpeg_ok = ffmpeg.returncode == 0
            interop_rows.append({
                "key": key,
                "encoded_path": str(encoded),
                "encoded_sha256": sha256(encoded),
                "faad_ok": faad_ok,
                "faad_returncode": faad.returncode,
                "faad_stderr": faad.stderr[-1000:],
                "ffmpeg_ok": ffmpeg_ok,
                "ffmpeg_returncode": ffmpeg.returncode,
                "ffmpeg_stderr": ffmpeg.stderr[-1000:],
            })
        if not faad_ok or not ffmpeg_ok:
            atomic_json(out_dir / "interop-results.json", {"streams": interop_rows})
            raise RuntimeError(f"Post-score decoder validation failed: {key} FAAD={faad.returncode} FFmpeg={ffmpeg.returncode}")
    counts = {"streams": len(interop_rows),
              "faad_pass": sum(item["faad_ok"] for item in interop_rows),
              "faad_fail": sum(not item["faad_ok"] for item in interop_rows),
              "ffmpeg_pass": sum(item["ffmpeg_ok"] for item in interop_rows),
              "ffmpeg_fail": sum(not item["ffmpeg_ok"] for item in interop_rows)}
    atomic_json(out_dir / "interop-results.json", {"schema": 1, "counts": counts, "streams": interop_rows})

    base_rows = {key.split("|", 1)[1]: row for key, row in run_rows.items()
                 if key.startswith("baseline_original|")}
    stack_rows = {key.split("|", 1)[1]: row for key, row in run_rows.items()
                  if key.startswith("stack_reference|")}
    summary = {"schema": 1, "phase": args.phase, "clips": clips, "rates_kbps": rates,
               "baseline": "baseline_original", "stack_reference": "stack_reference",
               "variants": {}, "decoder_counts": counts}
    pairs = []
    for variant in selected:
        reference_label = "stack_reference" if variant.endswith("_stack") else "baseline_original"
        pairs.append((variant, reference_label))
        per_clip, skipped, packet_ties, pcm_ties = [], [], 0, 0
        for clip in clips:
            cand_points, ref_points = [], []
            for rate in rates:
                cand = run_rows[f"{variant}|{clip}|{rate}"]
                ref = run_rows[f"{reference_label}|{clip}|{rate}"]
                cand_points.append((cand["actual_payload_kbps"], cand["mos"]))
                ref_points.append((ref["actual_payload_kbps"], ref["mos"]))
                packet_ties += cand["payload_sha256"] == ref["payload_sha256"]
                pcm_ties += cand["decoded_sha256"] == ref["decoded_sha256"]
            value = bd_rate_curve(ref_points, cand_points)
            if value is None:
                skipped.append(clip)
            else:
                per_clip.append({"clip": clip, "bd_rate_percent": value})
        values = [item["bd_rate_percent"] for item in per_clip]
        ci = bootstrap_mean_ci(values)
        summary["variants"][variant] = {
            "reference": reference_label,
            "per_clip_bd_rate": per_clip,
            "skipped_clips": skipped,
            "mean_bd_rate_percent": statistics.mean(values) if values else None,
            "median_bd_rate_percent": statistics.median(values) if values else None,
            "mean_bd_rate_bootstrap_95_ci": {"lo": ci["lo"], "hi": ci["hi"]} if ci else None,
            "packet_identity_ties": packet_ties,
            "pcm_identity_ties": pcm_ties,
            "rows": len(clips) * len(rates),
        }

    # Direct comparison to baseline is also reported for stack-conditioned
    # variants, while their primary matched control remains stack_reference.
    for variant in selected:
        if not variant.endswith("_stack"):
            continue
        pairs.append((variant, "baseline_original"))
    direct = {"clips": len(clips), "rates_kbps": rates, "comparisons": {}}
    for cand_name, ref_name in pairs:
        per_clip, packet_ties, pcm_ties = [], 0, 0
        for clip in clips:
            cand_points, ref_points = [], []
            for rate in rates:
                cand = run_rows[f"{cand_name}|{clip}|{rate}"]
                ref = run_rows[f"{ref_name}|{clip}|{rate}"]
                cand_points.append((cand["actual_payload_kbps"], cand["mos"]))
                ref_points.append((ref["actual_payload_kbps"], ref["mos"]))
                packet_ties += cand["payload_sha256"] == ref["payload_sha256"]
                pcm_ties += cand["decoded_sha256"] == ref["decoded_sha256"]
            value = bd_rate_curve(ref_points, cand_points)
            if value is not None:
                per_clip.append({"clip": clip, "bd_rate_percent": value})
        values = [item["bd_rate_percent"] for item in per_clip]
        ci = bootstrap_mean_ci(values)
        direct["comparisons"][f"{cand_name}_vs_{ref_name}"] = {
            "mean_bd_rate_percent": statistics.mean(values) if values else None,
            "median_bd_rate_percent": statistics.median(values) if values else None,
            "mean_bd_rate_bootstrap_95_ci": ci,
            "per_clip_bd_rate": per_clip,
            "valid_clips": len(values),
            "packet_identity_ties": packet_ties,
            "pcm_identity_ties": pcm_ties,
            "rows": len(clips) * len(rates),
        }
    atomic_json(out_dir / "direct-comparisons.json", direct)
    atomic_json(summary_path, summary)

    md = ["# HE-AAC v1 adaptive envelope-count experiment", "",
          f"Phase: {args.phase}; {len(clips)} clips at {', '.join(map(str, rates))} kbps.",
          "Every stream forces HE-AAC v1, is independently profile-checked, FDK-decoded before MOS scoring, and validated by strict FAAD3 and FFmpeg after scoring.",
          "Actual bitrate uses AAC packet bytes divided by source duration; BD-rate uses the benchmark PCHIP helper and clip-bootstrap intervals.", "",
          "| Variant | Matched reference | Mean BD-rate (95% CI) | Median | Packet ties | PCM ties |", "|---|---|---:|---:|---:|---:|"]
    for variant in selected:
        item = summary["variants"][variant]
        ci = item["mean_bd_rate_bootstrap_95_ci"]
        ci_text = "n/a" if ci is None else f"[{ci['lo']:+.3f}%, {ci['hi']:+.3f}%]"
        md.append(f"| {variant} | {item['reference']} | {item['mean_bd_rate_percent']:+.3f}% {ci_text} | "
                  f"{item['median_bd_rate_percent']:+.3f}% | {item['packet_identity_ties']}/{item['rows']} | "
                  f"{item['pcm_identity_ties']}/{item['rows']} |")
    md.extend(["", f"Post-score decoder validation: FAAD3 {counts['faad_pass']}/{counts['streams']}; "
               f"FFmpeg {counts['ffmpeg_pass']}/{counts['streams']}.",
               "Cached MOS is reused only after AAC packet and strict FDK-decoded PCM hashes match the prior measured stream.",
               "No winner or production claim is implied by a gate run.", ""])
    summary_md_path.write_text("\n".join(md))
    print(f"Saved rows: {result_path}")
    print(f"Saved summary: {summary_path}")
    return 0


if __name__ == "__main__":
    os.environ.setdefault("NUMBA_DISABLE_JIT", "1")
    raise SystemExit(main())
