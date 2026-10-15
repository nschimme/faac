#!/usr/bin/env python3
"""Reproducible missing-harmonic HE-AAC v1 SBR probe runner.

Run from faac-benchmark with its .venv Python. Results are written beneath
the probe checkout; completed rows are flushed atomically and can be resumed.
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
import subprocess
import sys
import tempfile
import threading
import wave
from datetime import datetime, timezone
from pathlib import Path

BENCH = Path("/Users/nschimme/gitprojects/faac-benchmark")
PROBE = Path("/private/tmp/faac-he-v1-harmonic")
BASE_BIN = Path("/private/tmp/faac-he-v1-probes/build-baseline/frontend/faac")
CAND_BIN = PROBE / "build-probe/frontend/faac"
FDK_BIN = BENCH / "bin/fdkdec"
OUT_ROOT = PROBE / "probe"

FEATURES = {
    "static24": {"FAAC_SBR_STATIC_NOISE_LEVEL": "24"},
    "missing_t10_r4": {"FAAC_SBR_MISSING_TONE": "10", "FAAC_SBR_MISSING_RATIO": "4"},
    "missing_t10_r10": {"FAAC_SBR_MISSING_TONE": "10", "FAAC_SBR_MISSING_RATIO": "10"},
    "missing_t30_r4": {"FAAC_SBR_MISSING_TONE": "30", "FAAC_SBR_MISSING_RATIO": "4"},
    "missing_t30_r10": {"FAAC_SBR_MISSING_TONE": "30", "FAAC_SBR_MISSING_RATIO": "10"},
    "missing_t100_r4": {"FAAC_SBR_MISSING_TONE": "100", "FAAC_SBR_MISSING_RATIO": "4"},
    "missing_t100_r10": {"FAAC_SBR_MISSING_TONE": "100", "FAAC_SBR_MISSING_RATIO": "10"},
    "missing_max_energy": {"FAAC_SBR_MISSING_TONE": "30", "FAAC_SBR_MISSING_RATIO": "4", "FAAC_SBR_HARMONIC_MAX_ENERGY": "1"},
    "missing_midpoint": {"FAAC_SBR_MISSING_TONE": "30", "FAAC_SBR_MISSING_RATIO": "4", "FAAC_SBR_HARMONIC_MIDPOINT_ONLY": "1"},
}
FEATURES["static18"] = {"FAAC_SBR_STATIC_NOISE_LEVEL": "18"}
for original in ("missing_t30_r4", "missing_midpoint", "missing_max_energy"):
    FEATURES[original + "_n18"] = dict(FEATURES[original], FAAC_SBR_STATIC_NOISE_LEVEL="18")

GATE_RATES = [32, 48, 64, 96]
GATE_CLIPS = ["sandman.16b48k.wav", "velvet.16b48k.wav",
              "21-classic.441.16b48k.wav", "fms.wav"]
PROBE_PREFIX = "FAAC_SBR_"


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def git_head(path: Path) -> str:
    return subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()


def source_tree_hash(path: Path) -> str:
    tracked = subprocess.check_output(["git", "-C", str(path), "ls-files", "-z"]).decode().split("\0")
    untracked = subprocess.check_output(["git", "-C", str(path), "ls-files", "--others", "--exclude-standard", "-z"]).decode().split("\0")
    files = sorted({x for x in tracked + untracked if x})
    h = hashlib.sha256()
    for name in files:
        p = path / name
        if p.is_file():
            h.update(name.encode("utf-8", "surrogateescape") + b"\0")
            h.update(p.read_bytes())
            h.update(b"\0")
    return h.hexdigest()


def source_diff_hash(path: Path) -> str:
    try:
        subprocess.check_output(["git", "-C", str(path), "rev-parse", "--verify", "upstream/master"],
                                stderr=subprocess.DEVNULL)
        committed = subprocess.check_output(["git", "-C", str(path), "diff", "--binary",
                                             "upstream/master...HEAD"])
    except subprocess.CalledProcessError:
        committed = subprocess.check_output(["git", "-C", str(path), "diff", "--binary", "HEAD"])
    staged = subprocess.check_output(["git", "-C", str(path), "diff", "--cached", "--binary"])
    working = subprocess.check_output(["git", "-C", str(path), "diff", "--binary"])
    return hashlib.sha256(committed + b"\0" + staged + b"\0" + working).hexdigest()


def linked_file_manifest(binary: Path) -> list[dict]:
    if not shutil.which("otool"):
        return []
    try:
        output = subprocess.check_output(["otool", "-L", str(binary)], text=True, stderr=subprocess.DEVNULL)
    except (OSError, subprocess.CalledProcessError):
        return []
    found = []
    for line in output.splitlines()[1:]:
        dep = line.strip().split(" (compatibility version", 1)[0]
        version_info = line.strip()[len(dep):].strip()
        candidate = Path(dep)
        if candidate.is_file():
            found.append({"path": str(candidate), "sha256": sha256(candidate), "version_info": version_info})
        else:
            found.append({"path": dep, "sha256": None, "version_info": version_info})
    return found


def atomic_json(path: Path, data) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n")
    os.replace(tmp, path)


def run(cmd, *, env=None, capture=True):
    return subprocess.run(cmd, check=True, text=True,
                          stdout=subprocess.PIPE if capture else None,
                          stderr=subprocess.PIPE if capture else None, env=env)


def media_duration(path: Path) -> float:
    with wave.open(str(path), "rb") as w:
        return w.getnframes() / w.getframerate()


def stream_profile(path: Path) -> str:
    p = run(["ffprobe", "-v", "error", "-select_streams", "a:0", "-show_entries",
             "stream=profile", "-of", "json", str(path)])
    streams = json.loads(p.stdout).get("streams", [])
    if len(streams) != 1:
        raise RuntimeError(f"Expected one audio stream in {path}, got {len(streams)}")
    return streams[0].get("profile", "")


def payload_bitrate_kbps(path: Path, duration: float) -> float:
    p = run(["ffprobe", "-v", "error", "-select_streams", "a:0", "-show_packets",
             "-show_entries", "packet=size", "-of", "json", str(path)])
    sizes = [int(pkt["size"]) for pkt in json.loads(p.stdout).get("packets", [])
             if "size" in pkt]
    if not sizes or duration <= 0:
        raise RuntimeError(f"No audio packet sizes/duration for {path}")
    return sum(sizes) * 8.0 / duration / 1000.0


def payload_sha256(path: Path) -> str:
    p = run(["ffprobe", "-v", "error", "-select_streams", "a:0", "-show_packets",
             "-show_data_hash", "sha256", "-show_entries", "packet=size,data_hash",
             "-of", "json", str(path)])
    packets = json.loads(p.stdout).get("packets", [])
    if not packets:
        raise RuntimeError(f"No audio packets for payload identity: {path}")
    h = hashlib.sha256()
    for packet in packets:
        if "size" not in packet or "data_hash" not in packet:
            raise RuntimeError(f"ffprobe omitted packet size/hash for {path}")
        h.update(str(int(packet["size"])).encode("ascii"))
        h.update(b":")
        h.update(packet["data_hash"].encode("ascii"))
        h.update(b"\n")
    return h.hexdigest()


def convert_reference(src: Path, dst: Path, utils) -> None:
    if not utils.wav_conv(str(src), str(dst), rate=48000, channels=2):
        raise RuntimeError(f"Could not convert reference WAV: {src}")


def decode_and_convert(encoded: Path, dst: Path, decoder, utils, temp_dir: Path) -> None:
    decoded = temp_dir / (dst.stem + ".fdk.wav")
    decoder_env = dict(os.environ)
    decoder_env.update(decoder.get_run_env())
    run(decoder.get_decode_cmd(str(encoded), str(decoded)), env=decoder_env)
    if not decoded.is_file() or decoded.stat().st_size == 0:
        raise RuntimeError(f"FDK decoder produced no WAV for {encoded}")
    if not utils.wav_conv(str(decoded), str(dst), rate=48000, channels=2):
        raise RuntimeError(f"Could not convert decoded WAV: {decoded}")


def encode(binary: Path, source: Path, out: Path, rate: int, env_values: dict,
           encoder_cls) -> tuple[float, str]:
    encoder = encoder_cls("faac-he-v1-probe", str(binary), profile="he")
    cmd = encoder.get_encode_cmd(str(source), str(out), rate, 2, 48000)
    if "--object-type" not in cmd or cmd[cmd.index("--object-type") + 1] != "he-aac-v1":
        raise RuntimeError(f"Encoder command does not force HE-AAC v1: {cmd}")
    env = dict(os.environ)
    for key in list(env):
        if key.startswith(PROBE_PREFIX):
            del env[key]
    env.update(env_values)
    run(cmd, env=env)
    profile = stream_profile(out)
    if profile != "HE-AAC":
        raise RuntimeError(f"Expected ffprobe profile HE-AAC, got {profile!r} for {out}")
    return payload_bitrate_kbps(out, media_duration(source)), profile


def load_json(path: Path, default):
    if not path.exists():
        return default
    return json.loads(path.read_text())


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--variants", default="all",
                    help="comma-separated feature names; default all (controls always run)")
    ap.add_argument("--clips", choices=("gate", "all"), default="gate")
    ap.add_argument("--rates", default="32,48,64,96")
    ap.add_argument("--jobs", type=int, default=4, help="parallel feature-row workers (default: 4)")
    ap.add_argument("--output-dir", help="result directory; defaults to probe/results-<clips>")
    args = ap.parse_args()

    sys.path.insert(0, str(BENCH))
    from codec_bench.encoders import FAACEncoder
    from codec_bench.decoders import FDKDecoder
    from config import _MUSIC_GATE
    import utils
    from phase2_mos import score_wav_pair
    from compare_results import bootstrap_mean_ci

    out_dir = Path(args.output_dir).expanduser().resolve() if args.output_dir else OUT_ROOT / f"results-{args.clips}"
    out_dir.mkdir(parents=True, exist_ok=True)
    results_path = out_dir / "harmonic-results.json"
    metadata_path = out_dir / "harmonic-config.json"
    summary_json_path = out_dir / "harmonic-summary.json"
    summary_md_path = out_dir / "RESULTS.md"
    for binary in (BASE_BIN, CAND_BIN, FDK_BIN):
        if not binary.is_file():
            raise FileNotFoundError(binary)
    rates = [int(x) for x in args.rates.split(",") if x]
    if not rates or any(r <= 0 for r in rates) or args.jobs <= 0:
        ap.error("--rates must be positive comma-separated kbps values")
    if args.clips == "gate":
        clips = list(_MUSIC_GATE)
        if clips != GATE_CLIPS:
            raise RuntimeError(f"Benchmark gate changed: expected {GATE_CLIPS}, got {clips}")
    else:
        clips = sorted(p.name for p in (BENCH / "data/external/audio").glob("*.wav"))
    selected = list(FEATURES) if args.variants == "all" else [x for x in args.variants.split(",") if x]
    unknown = sorted(set(selected) - set(FEATURES))
    if unknown:
        ap.error(f"unknown variants: {unknown}; choices: {','.join(FEATURES)}")
    variants_to_run = list(dict.fromkeys(selected + ["static24"]))

    clip_hashes = {clip: sha256(BENCH / "data/external/audio" / clip) for clip in clips}
    binary_manifest = {}
    for binary in (BASE_BIN, CAND_BIN, FDK_BIN):
        binary_manifest[str(binary)] = {
            "sha256": sha256(binary), "size_bytes": binary.stat().st_size,
            "linked_libraries": linked_file_manifest(binary),
        }
    # On macOS the FAAC binaries normally resolve these via @rpath; capture
    # the exact in-tree dylibs as well, independent of the current loader path.
    for label, dylib in (("baseline", Path("/private/tmp/faac-he-v1-probes/build-baseline/libfaac/libfaac.2.dylib")),
                         ("harmonic", PROBE / "build-probe/libfaac/libfaac.2.dylib")):
        if dylib.is_file():
            binary_manifest[f"{label}_libfaac"] = {"path": str(dylib), "sha256": sha256(dylib),
                                                    "size_bytes": dylib.stat().st_size}
    identity = {
        "benchmark_commit": git_head(BENCH), "probe_commit": git_head(PROBE),
        "source_tree_sha256": source_tree_hash(PROBE), "source_diff_sha256": source_diff_hash(PROBE),
        "binaries": binary_manifest,
        "clips": clips, "clip_sha256": clip_hashes, "rates_kbps": rates, "variants": selected,
        "feature_env": FEATURES,
    }
    metadata = {
        "schema": 1, "created_utc": datetime.now(timezone.utc).isoformat(),
        "benchmark_root": str(BENCH), "benchmark_commit": identity["benchmark_commit"],
        "probe_root": str(PROBE), "probe_commit": identity["probe_commit"],
        "binaries": identity["binaries"], "source_tree_sha256": identity["source_tree_sha256"],
        "source_diff_sha256": identity["source_diff_sha256"],
        "decoder": "FDK AAC via codec_bench.decoders.FDKDecoder; strict command failure",
        "metric": "phase2_mos.score_wav_pair(mode_str='audio', sample_rate=48000) / Zimtohrli",
        "clips": clips, "rates_kbps": rates, "variants": selected,
        "controls": ["baseline_default", "candidate_default"],
        "jobs": args.jobs,
        "profile": "HE-AAC v1; forced by FAACEncoder(profile='he') command",
        "actual_bitrate": "sum(ffprobe audio packet size) * 8 / input WAV duration",
        "clip_sha256": clip_hashes, "output_dir": str(out_dir),
        "feature_env": FEATURES, "immutable_run_config": identity,
    }
    old_meta = load_json(metadata_path, {})
    if old_meta:
        old_identity = old_meta.get("immutable_run_config", {})
        provenance_keys = {"source_tree_sha256", "source_diff_sha256"}
        old_core = {k: v for k, v in old_identity.items() if k not in provenance_keys}
        new_core = {k: v for k, v in identity.items() if k not in provenance_keys}
        if old_core != new_core:
            raise RuntimeError("Existing result metadata does not match this run; choose a different --output-dir")
        if any(old_identity.get(k) != identity.get(k) for k in provenance_keys):
            metadata["resumed_after_source_metadata_change"] = {
                "previous_source_tree_sha256": old_identity.get("source_tree_sha256"),
                "previous_source_diff_sha256": old_identity.get("source_diff_sha256"),
                "current_source_tree_sha256": identity["source_tree_sha256"],
                "current_source_diff_sha256": identity["source_diff_sha256"],
                "note": "Binary hashes and all run inputs match; source hashes changed after the encoded artifacts were produced.",
            }
    atomic_json(metadata_path, metadata)
    results = load_json(results_path, {"schema": 1, "rows": {}})
    source_dir = BENCH / "data/external/audio"
    cache_dir = out_dir / "cache"
    encode_dir = out_dir / "encodes"
    wav_dir = out_dir / "scoring-wav"
    for d in (cache_dir, encode_dir, wav_dir):
        d.mkdir(parents=True, exist_ok=True)
    decoder = FDKDecoder("FDK AAC", str(FDK_BIN))

    # Score references once and cache them in the suite's required 48 kHz stereo form.
    references = {}
    for clip in clips:
        src = source_dir / clip
        if not src.is_file():
            raise FileNotFoundError(src)
        with wave.open(str(src), "rb") as w:
            if w.getframerate() != 48000 or w.getnchannels() != 2:
                raise RuntimeError(f"Expected 48 kHz stereo source WAV: {src}")
        ref = cache_dir / f"ref-{clip}"
        if not ref.is_file():
            convert_reference(src, ref, utils)
        references[clip] = ref

    results_lock = threading.Lock()

    def persist_result(key, row):
        with results_lock:
            results["rows"][key] = row
            atomic_json(results_path, results)

    def ensure_control_row(label, binary, env_values, clip, rate, *, score):
        src = source_dir / clip
        key = f"{label}|{clip}|{rate}"
        enc = encode_dir / label / str(rate) / (Path(clip).stem + ".m4a")
        enc.parent.mkdir(parents=True, exist_ok=True)
        decoded = wav_dir / label / str(rate) / (Path(clip).stem + ".wav")
        decoded.parent.mkdir(parents=True, exist_ok=True)
        old_row = results["rows"].get(key)
        if (old_row and enc.is_file() and decoded.is_file()
                and old_row.get("encoded_sha256") == sha256(enc)
                and old_row.get("decoded_sha256") == sha256(decoded)):
            actual = old_row["actual_payload_kbps"]
            profile = old_row["profile"]
            mos = old_row.get("mos")
            backend = old_row.get("metric_backend")
            packet_hash = old_row.get("payload_sha256") or payload_sha256(enc)
        else:
            with results_lock:
                results["rows"].pop(key, None)
            enc.unlink(missing_ok=True)
            decoded.unlink(missing_ok=True)
            actual, profile = encode(binary, src, enc, rate, env_values, FAACEncoder)
            with tempfile.TemporaryDirectory(prefix="faac-probe-decode-") as td:
                decode_and_convert(enc, decoded, decoder, utils, Path(td))
            packet_hash = payload_sha256(enc)
            mos = backend = None
        if profile != "HE-AAC":
            raise RuntimeError(f"Invalid profile in {label}/{clip}/{rate}: {profile}")
        if score and mos is None:
            mos, backend = score_wav_pair(str(references[clip]), str(decoded), mode_str="audio", sample_rate=48000)
            if mos is None or backend != "zimtohrli" or not math.isfinite(float(mos)):
                raise RuntimeError(f"Strict audio scoring failed: {clip}/{rate}/{label}: {mos}, {backend}")
        elif score and (backend != "zimtohrli" or not math.isfinite(float(mos))):
            raise RuntimeError(f"Invalid cached MOS for {clip}/{rate}/{label}: {mos}, {backend}")
        row = {
            "variant": label, "clip": clip, "target_kbps": rate,
            "actual_payload_kbps": actual, "profile": profile,
            "mos": float(mos) if mos is not None else None,
            "metric_backend": backend, "mos_source": "scored" if score else None,
            "encoded_path": str(enc), "encoded_sha256": sha256(enc),
            "payload_sha256": packet_hash,
            "decoded_path": str(decoded), "decoded_sha256": sha256(decoded),
        }
        persist_result(key, row)
        print(f"completed {label} {rate} kbps {clip}: "
              f"MOS={'pending' if mos is None else f'{float(mos):.5f}'}, payload={actual:.3f} kbps", flush=True)

    def run_control_pair(clip, rate):
        ensure_control_row("baseline_default", BASE_BIN, {}, clip, rate, score=True)
        ensure_control_row("candidate_default", CAND_BIN, {}, clip, rate, score=False)
        base = results["rows"][f"baseline_default|{clip}|{rate}"]
        control = results["rows"][f"candidate_default|{clip}|{rate}"]
        if base["payload_sha256"] != control["payload_sha256"]:
            raise RuntimeError(f"Probe default AAC payload differs from baseline: {clip}/{rate}")
        if base["decoded_sha256"] != control["decoded_sha256"]:
            raise RuntimeError(f"Probe default decoded PCM differs from baseline: {clip}/{rate}")
        # AAC payload and decoded PCM identity make a fresh metric invocation redundant.
        control = dict(control)
        control["mos"] = base["mos"]
        control["metric_backend"] = base["metric_backend"]
        control["mos_source"] = "pcm_identity"
        persist_result(f"candidate_default|{clip}|{rate}", control)
        return

    # Each clip/rate pair owns separate files; process control pairs in parallel,
    # and wait for every identity check before scheduling any feature variant.
    control_jobs = [(clip, rate) for clip in clips for rate in rates]
    with ThreadPoolExecutor(max_workers=args.jobs) as executor:
        futures = [executor.submit(run_control_pair, clip, rate) for clip, rate in control_jobs]
        for future in as_completed(futures):
            future.result()

    def run_feature_row(variant, clip, rate):
        key = f"{variant}|{clip}|{rate}"
        src = source_dir / clip
        enc = encode_dir / variant / str(rate) / (Path(clip).stem + ".m4a")
        enc.parent.mkdir(parents=True, exist_ok=True)
        decoded = wav_dir / variant / str(rate) / (Path(clip).stem + ".wav")
        decoded.parent.mkdir(parents=True, exist_ok=True)
        old_row = results["rows"].get(key)
        if (old_row and enc.is_file() and decoded.is_file()
                and old_row.get("encoded_sha256") == sha256(enc)
                and old_row.get("decoded_sha256") == sha256(decoded)):
            if not old_row.get("payload_sha256"):
                old_row["payload_sha256"] = payload_sha256(enc)
                with results_lock:
                    atomic_json(results_path, results)
            return
        with results_lock:
            results["rows"].pop(key, None)
        enc.unlink(missing_ok=True)
        decoded.unlink(missing_ok=True)
        actual, profile = encode(CAND_BIN, src, enc, rate, FEATURES[variant], FAACEncoder)
        with tempfile.TemporaryDirectory(prefix="faac-probe-decode-") as td:
            decode_and_convert(enc, decoded, decoder, utils, Path(td))
        mos, backend = score_wav_pair(str(references[clip]), str(decoded), mode_str="audio", sample_rate=48000)
        if mos is None or backend != "zimtohrli" or not math.isfinite(float(mos)):
            raise RuntimeError(f"Strict audio scoring failed: {clip}/{rate}/{variant}: {mos}, {backend}")
        if profile != "HE-AAC":
            raise RuntimeError(f"Invalid profile in {variant}/{clip}/{rate}: {profile}")
        row = {
            "variant": variant, "clip": clip, "target_kbps": rate,
            "actual_payload_kbps": actual, "profile": profile,
            "mos": float(mos), "metric_backend": backend,
            "encoded_path": str(enc), "encoded_sha256": sha256(enc),
            "payload_sha256": payload_sha256(enc),
            "decoded_path": str(decoded), "decoded_sha256": sha256(decoded),
        }
        persist_result(key, row)
        print(f"completed {variant} {rate} kbps {clip}: MOS={float(mos):.5f}, payload={actual:.3f} kbps", flush=True)

    feature_jobs = [(variant, clip, rate) for clip in clips for rate in rates for variant in variants_to_run]
    with ThreadPoolExecutor(max_workers=args.jobs) as executor:
        futures = [executor.submit(run_feature_row, *job) for job in feature_jobs]
        for future in as_completed(futures):
            future.result()

    # Per-rate paired summaries, with only the benchmark suite's bootstrap helper.
    for rate in rates:
        base_rows = {c: results["rows"][f"baseline_default|{c}|{rate}"] for c in clips}
        for variant in ["candidate_default"] + selected:
            deltas = [results["rows"][f"{variant}|{c}|{rate}"]["mos"] - base_rows[c]["mos"] for c in clips]
            rate_deltas = [100.0 * (results["rows"][f"{variant}|{c}|{rate}"]["actual_payload_kbps"] /
                                    base_rows[c]["actual_payload_kbps"] - 1.0) for c in clips]
            ci = bootstrap_mean_ci(deltas)
            detail = (f"MOS Δ={sum(deltas)/len(deltas):+.5f}, bitrate Δ={sum(rate_deltas)/len(rate_deltas):+.3f}%")
            if ci:
                detail += f", bootstrap mean 95% CI=[{ci['lo']:+.5f}, {ci['hi']:+.5f}]"
            else:
                detail += f", MOS min/max=[{min(deltas):+.5f}, {max(deltas):+.5f}] (n={len(deltas)}; no CI)"
            print(f"{variant} @ {rate} kbps: {detail}", flush=True)
    # The benchmark's PCHIP BD-rate implementation handles uneven actual
    # payload rates and reports None where curves lack sufficient overlap.
    scripts_dir = str(BENCH / "scripts")
    if scripts_dir not in sys.path:
        sys.path.insert(0, scripts_dir)
    from bd_rate import bd_rate_curve
    import statistics
    summary = {"schema": 1, "method": "scripts/bd_rate.py:bd_rate_curve",
               "baseline": "baseline_default", "rungs_kbps": rates, "variants": {}}
    lines = ["# HE-AAC v1 missing-harmonic SBR probe summary", "",
             "BD-rate uses actual ffprobe packet payload rate and Zimtohrli MOS. Positive means more bits are required for equal quality.", "",
             "| Variant | Valid clips | Mean BD-rate (95% clip bootstrap CI) | Median BD-rate |", "|---|---:|---:|---:|"]
    for variant in ["candidate_default"] + selected:
        per_clip = []
        skipped = []
        for clip in clips:
            base_points = [(results["rows"][f"baseline_default|{clip}|{rate}"]["actual_payload_kbps"],
                            results["rows"][f"baseline_default|{clip}|{rate}"]["mos"]) for rate in rates]
            cand_points = [(results["rows"][f"{variant}|{clip}|{rate}"]["actual_payload_kbps"],
                            results["rows"][f"{variant}|{clip}|{rate}"]["mos"]) for rate in rates]
            value = bd_rate_curve(base_points, cand_points)
            if value is None:
                skipped.append(clip)
            else:
                per_clip.append({"clip": clip, "bd_rate_percent": value})
        values = [x["bd_rate_percent"] for x in per_clip]
        import statistics
        ci = bootstrap_mean_ci(values)
        item = {"per_clip": per_clip, "valid_count": len(values), "skipped_clips": skipped,
                "mean_percent": statistics.mean(values) if values else None,
                "median_percent": statistics.median(values) if values else None,
                "mean_bootstrap_95_ci": {"lo": ci["lo"], "hi": ci["hi"]} if ci else None}
        summary["variants"][variant] = item
        mean = "n/a" if item["mean_percent"] is None else f"{item['mean_percent']:+.3f}%"
        ci_text = "n/a" if ci is None else f"[{ci['lo']:+.3f}%, {ci['hi']:+.3f}%]"
        median = "n/a" if item["median_percent"] is None else f"{item['median_percent']:+.3f}%"
        lines.append(f"| {variant} | {len(values)}/{len(clips)} | {mean} {ci_text} | {median} |")
        item["per_rate"] = {}
        for rate in rates:
            deltas, rate_deltas = [], []
            base_mos, cand_mos = [], []
            for clip in clips:
                base = results["rows"][f"baseline_default|{clip}|{rate}"]
                candidate = results["rows"][f"{variant}|{clip}|{rate}"]
                deltas.append(candidate["mos"] - base["mos"])
                rate_deltas.append(100.0 * (candidate["actual_payload_kbps"] / base["actual_payload_kbps"] - 1.0))
                base_mos.append(base["mos"])
                cand_mos.append(candidate["mos"])
            mos_ci = bootstrap_mean_ci(deltas)
            item["per_rate"][str(rate)] = {
                "mean_baseline_mos": statistics.mean(base_mos),
                "mean_variant_mos": statistics.mean(cand_mos),
                "mean_mos_delta": statistics.mean(deltas),
                "mos_delta_bootstrap_95_ci": {"lo": mos_ci["lo"], "hi": mos_ci["hi"]} if mos_ci else None,
                "mean_actual_rate_delta_percent": statistics.mean(rate_deltas),
            }

    # Direct comparisons retain the paired clip bootstrap for the BD-rate
    # estimate and for each rate's MOS delta.
    direct = {"clips": len(clips), "rates_kbps": rates, "comparisons": {}}
    for cand_name in selected:
        references = ["baseline_default"] if cand_name == "static24" else ["baseline_default", "static24"]
        for ref_name in references:
            per_clip, packet_ties, pcm_ties = [], 0, 0
            for clip in clips:
                base_points, cand_points = [], []
                for rate in rates:
                    ref = results["rows"][f"{ref_name}|{clip}|{rate}"]
                    candidate = results["rows"][f"{cand_name}|{clip}|{rate}"]
                    base_points.append((ref["actual_payload_kbps"], ref["mos"]))
                    cand_points.append((candidate["actual_payload_kbps"], candidate["mos"]))
                    packet_ties += ref["payload_sha256"] == candidate["payload_sha256"]
                    pcm_ties += ref["decoded_sha256"] == candidate["decoded_sha256"]
                value = bd_rate_curve(base_points, cand_points)
                if value is not None:
                    per_clip.append({"clip": clip, "bd_rate_percent": value})
            vals = [x["bd_rate_percent"] for x in per_clip]
            bd_ci = bootstrap_mean_ci(vals)
            per_rate = {}
            for rate in rates:
                mos_deltas, bitrate_deltas = [], []
                for clip in clips:
                    ref = results["rows"][f"{ref_name}|{clip}|{rate}"]
                    candidate = results["rows"][f"{cand_name}|{clip}|{rate}"]
                    mos_deltas.append(candidate["mos"] - ref["mos"])
                    bitrate_deltas.append(100.0 * (candidate["actual_payload_kbps"] / ref["actual_payload_kbps"] - 1.0))
                rate_ci = bootstrap_mean_ci(mos_deltas)
                per_rate[str(rate)] = {
                    "mean_mos_delta": statistics.mean(mos_deltas),
                    "mos_delta_bootstrap_95_ci": {"lo": rate_ci["lo"], "hi": rate_ci["hi"]} if rate_ci else None,
                    "mean_actual_bitrate_delta_percent": statistics.mean(bitrate_deltas),
                }
            direct["comparisons"][f"{cand_name}_vs_{ref_name}"] = {
                "mean_bd_rate_percent": statistics.mean(vals) if vals else None,
                "median_bd_rate_percent": statistics.median(vals) if vals else None,
                "mean_bd_rate_bootstrap_95_ci": {"lo": bd_ci["lo"], "hi": bd_ci["hi"]} if bd_ci else None,
                "per_clip_bd_rate": per_clip, "valid_clips": len(vals),
                "aac_packet_identity_ties": packet_ties, "decoded_pcm_identity_ties": pcm_ties,
                "rows": len(clips) * len(rates), "per_rate": per_rate,
            }
    atomic_json(out_dir / "direct-comparisons.json", direct)
    lines.extend(["", "## Direct comparisons", "",
                  "Negative BD-rate favors the candidate; intervals bootstrap over clips.", "",
                  "| Candidate vs reference | Mean BD-rate (95% CI) | Median | Packet ties | PCM ties |",
                  "|---|---:|---:|---:|---:|"])
    for pair, item in direct["comparisons"].items():
        ci = item["mean_bd_rate_bootstrap_95_ci"]
        ci_text = "n/a" if ci is None else f"[{ci['lo']:+.3f}%, {ci['hi']:+.3f}%]"
        mean = "n/a" if item["mean_bd_rate_percent"] is None else f"{item['mean_bd_rate_percent']:+.3f}%"
        median = "n/a" if item["median_bd_rate_percent"] is None else f"{item['median_bd_rate_percent']:+.3f}%"
        lines.append(f"| {pair} | {mean} {ci_text} | {median} | {item['aac_packet_identity_ties']}/{item['rows']} | {item['decoded_pcm_identity_ties']}/{item['rows']} |")
    atomic_json(summary_json_path, summary)
    summary_md_path.write_text("\n".join(lines) + "\n")
    print(f"Saved rows: {results_path}")
    print(f"Saved BD-rate summary: {summary_json_path}")
    return 0


if __name__ == "__main__":
    # The benchmark's MOS engine imports NumPy/Numba; disable JIT in the
    # constrained runner environment while retaining its scoring function.
    os.environ.setdefault("NUMBA_DISABLE_JIT", "1")
    raise SystemExit(main())
