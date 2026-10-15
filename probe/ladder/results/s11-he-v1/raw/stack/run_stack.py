#!/usr/bin/env python3
"""Controlled HE-AAC v1 inverse-filtering and noise-floor experiment.

Run from faac-benchmark with its virtualenv Python. This is a measurement
runner only; it makes no shipping recommendation.
"""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
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
from datetime import datetime, timezone
from pathlib import Path

BENCH = Path("/Users/nschimme/gitprojects/faac-benchmark")
PROBE = Path("/private/tmp/faac-he-v1-stack")
BASE_BIN = Path("/private/tmp/faac-he-v1-probes/build-baseline/frontend/faac")
CAND_BIN = PROBE / "build-stack/frontend/faac"
FDK_BIN = BENCH / "bin/fdkdec"
OLD_RUN_ROOT = Path("/private/tmp/faac-he-v1-probes/probe")
ADAPTIVE_RUN_ROOT = Path("/private/tmp/faac-he-v1-adaptive/probe")
GATE_CLIPS = ["sandman.16b48k.wav", "velvet.16b48k.wav",
              "21-classic.441.16b48k.wav", "fms.wav"]
PROBE_PREFIX = "FAAC_SBR_"

FEATURES = {
    "limiter": {"FAAC_SBR_LIMITER": "1"},
    "static18": {"FAAC_SBR_STATIC_NOISE_LEVEL": "18"},
    "static24": {"FAAC_SBR_STATIC_NOISE_LEVEL": "24"},
    "stack18": {"FAAC_SBR_LIMITER": "1", "FAAC_SBR_STATIC_NOISE_LEVEL": "18"},
    "stack24": {"FAAC_SBR_LIMITER": "1", "FAAC_SBR_STATIC_NOISE_LEVEL": "24"},
}
RATES = [32, 48, 64, 96]


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def source_tree_hash(path: Path) -> str:
    tracked = subprocess.check_output(["git", "-C", str(path), "ls-files", "-z"]).decode().split("\0")
    untracked = subprocess.check_output(["git", "-C", str(path), "ls-files", "--others", "--exclude-standard", "-z"]).decode().split("\0")
    h = hashlib.sha256()
    for name in sorted({n for n in tracked + untracked if n}):
        p = path / name
        if p.is_file():
            h.update(name.encode("utf-8", "surrogateescape") + b"\0")
            h.update(p.read_bytes() + b"\0")
    return h.hexdigest()


def source_diff_hash(path: Path) -> str:
    try:
        subprocess.check_output(["git", "-C", str(path), "rev-parse", "--verify", "upstream/master"], stderr=subprocess.DEVNULL)
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
        text = subprocess.check_output(["otool", "-L", str(binary)], text=True,
                                       stderr=subprocess.DEVNULL)
    except (OSError, subprocess.CalledProcessError):
        return []
    items = []
    for line in text.splitlines()[1:]:
        dep = line.strip().split(" (compatibility version", 1)[0]
        version = line.strip()[len(dep):].strip()
        path = Path(dep)
        items.append({"path": dep, "sha256": sha256(path) if path.is_file() else None,
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
    p = run(["ffprobe", "-v", "error", "-select_streams", "a:0", "-show_packets",
             "-show_data_hash", "sha256", "-show_entries", "packet=size,data_hash",
             "-of", "json", str(path)])
    packets = json.loads(p.stdout).get("packets", [])
    if not packets:
        raise RuntimeError(f"ffprobe found no AAC packets: {path}")
    h = hashlib.sha256()
    for packet in packets:
        if "size" not in packet or "data_hash" not in packet:
            raise RuntimeError(f"ffprobe omitted packet identity fields: {path}")
        h.update(str(int(packet["size"])).encode("ascii") + b":" + packet["data_hash"].encode("ascii") + b"\n")
    return h.hexdigest()


def payload_bitrate(path: Path, duration: float) -> float:
    p = run(["ffprobe", "-v", "error", "-select_streams", "a:0", "-show_packets",
             "-show_entries", "packet=size", "-of", "json", str(path)])
    sizes = [int(x["size"]) for x in json.loads(p.stdout).get("packets", []) if "size" in x]
    if not sizes or duration <= 0:
        raise RuntimeError(f"Could not calculate payload bitrate: {path}")
    return sum(sizes) * 8.0 / duration / 1000.0


def profile(path: Path) -> str:
    p = run(["ffprobe", "-v", "error", "-select_streams", "a:0", "-show_entries",
             "stream=profile", "-of", "json", str(path)])
    streams = json.loads(p.stdout).get("streams", [])
    if len(streams) != 1:
        raise RuntimeError(f"Expected one audio stream in {path}")
    return streams[0].get("profile", "")


def wav_duration(path: Path) -> float:
    with wave.open(str(path), "rb") as w:
        return w.getnframes() / w.getframerate()


def encode(binary, src, output, rate, env_values, FAACEncoder):
    encoder = FAACEncoder("faac-noise-probe", str(binary), profile="he")
    command = encoder.get_encode_cmd(str(src), str(output), rate, 2, 48000)
    if "--object-type" not in command or command[command.index("--object-type") + 1] != "he-aac-v1":
        raise RuntimeError(f"Encoding command does not force HE-AAC v1: {command}")
    env = dict(os.environ)
    for key in list(env):
        if key.startswith(PROBE_PREFIX):
            env.pop(key)
    env.update(env_values)
    proc = run(command, env=env)
    if profile(output) != "HE-AAC":
        raise RuntimeError(f"Expected HE-AAC profile from {output}")
    return proc.stderr[-4000:], payload_bitrate(output, wav_duration(src))


def decode_and_convert(encoded, output, decoder, utils, temp_dir):
    decoded = temp_dir / (output.stem + ".fdk.wav")
    env = dict(os.environ)
    env.update(decoder.get_run_env())
    run(decoder.get_decode_cmd(str(encoded), str(decoded)), env=env)
    if not decoded.is_file() or decoded.stat().st_size == 0:
        raise RuntimeError(f"Strict FDK decode produced no output for {encoded}")
    if not utils.wav_conv(str(decoded), str(output), rate=48000, channels=2):
        raise RuntimeError(f"Could not convert decoded output to 48 kHz stereo: {decoded}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clips", choices=("gate", "all"), default="gate")
    parser.add_argument("--variants", default="all", help="comma-separated variants or all")
    parser.add_argument("--rates", default="32,48,64,96")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--output-dir", type=Path, help="defaults to probe/results-gate or probe/results-all")
    args = parser.parse_args()
    if args.jobs <= 0:
        parser.error("--jobs must be positive")
    rates = [int(x) for x in args.rates.split(",") if x]
    if not rates or any(x <= 0 for x in rates):
        parser.error("--rates must be positive comma-separated kbps")
    selected = list(FEATURES) if args.variants == "all" else [x.strip() for x in args.variants.split(",") if x.strip()]
    unknown = sorted(set(selected) - set(FEATURES))
    if unknown:
        parser.error(f"Unknown variants {unknown}; choices: {','.join(FEATURES)}")
    run_variants = list(dict.fromkeys(selected + ["static18", "static24"] + [
        name.replace("joint_", "invf_") for name in selected
        if name in ("joint_raw12", "joint_raw18", "joint_db12", "joint_db18")
    ]))

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

    if args.clips == "gate":
        clips = list(_MUSIC_GATE)
        if clips != GATE_CLIPS:
            raise RuntimeError(f"Gate clip list changed: {clips}")
    else:
        clips = sorted(p.name for p in (BENCH / "data/external/audio").glob("*.wav"))
    out_dir = args.output_dir.expanduser().resolve() if args.output_dir else PROBE / "probe" / f"results-{args.clips}"
    try:
        out_dir.relative_to((PROBE / "probe").resolve())
    except ValueError:
        raise RuntimeError(f"Keep experiment output under {PROBE / 'probe'}")
    out_dir.mkdir(parents=True, exist_ok=True)
    result_path = out_dir / "stack-results.json"
    config_path = out_dir / "stack-config.json"
    summary_path = out_dir / "stack-summary.json"
    summary_md_path = out_dir / "RESULTS.md"

    for binary in (BASE_BIN, CAND_BIN, FDK_BIN):
        if not binary.is_file():
            raise FileNotFoundError(binary)
    source_dir = BENCH / "data/external/audio"
    clip_hashes = {clip: sha256(source_dir / clip) for clip in clips}
    previous_results_paths = {
        "baseline": OLD_RUN_ROOT / f"results-{args.clips}" / "feature-results.json",
        "adaptive": ADAPTIVE_RUN_ROOT / f"results-{args.clips}" / "noise-results.json",
    }
    for path in previous_results_paths.values():
        if not path.is_file():
            raise FileNotFoundError(path)
    library_paths = [
        Path("/private/tmp/faac-he-v1-probes/build-baseline/libfaac/libfaac.2.dylib"),
        PROBE / "build-probe/libfaac/libfaac.2.dylib",
    ]
    identity = {
        "benchmark_commit": git_head(BENCH), "probe_commit": git_head(PROBE),
        "source_tree_sha256": source_tree_hash(PROBE), "source_diff_sha256": source_diff_hash(PROBE),
        "binaries": {str(b): sha256(b) for b in (BASE_BIN, CAND_BIN, FDK_BIN)},
        "linked_libraries": {str(b): linked_libraries(b) for b in (BASE_BIN, CAND_BIN, FDK_BIN)},
        "libraries": {str(p): sha256(p) for p in library_paths if p.is_file()},
        "clip_sha256": clip_hashes, "clips": clips, "rates_kbps": rates,
        "variants": selected, "measured_reference_variants": run_variants,
        "feature_environment": FEATURES,
        "prior_measurement_results": {k: str(v) for k, v in previous_results_paths.items()},
        "prior_measurement_results_sha256": {k: sha256(v) for k, v in previous_results_paths.items()},
        "calibration_note": "Independent opt-in limiter header and static noise floor overrides; noise values are 18 and 24.",
    }
    config = {
        "schema": 1, "created_utc": datetime.now(timezone.utc).isoformat(),
        "immutable_run_config": identity, **identity,
        "candidate_binary": str(CAND_BIN), "baseline_binary": str(BASE_BIN),
        "decoder": "FDK AAC strict; no fallback", "metric": "Zimtohrli via phase2_mos.score_wav_pair audio mode at 48 kHz",
        "actual_bitrate": "ffprobe audio packet bytes * 8 / source WAV duration",
        "workers": args.jobs,
    }
    old_config = json.loads(config_path.read_text()) if config_path.exists() else None
    if old_config and old_config.get("immutable_run_config") != identity:
        raise RuntimeError("Output directory has different provenance/configuration; choose a new --output-dir")
    atomic_json(config_path, config)

    old_results = json.loads(result_path.read_text()) if result_path.exists() else {"schema": 1, "rows": {}}
    baseline_rows = json.loads(previous_results_paths["baseline"].read_text())["rows"]
    adaptive_rows = json.loads(previous_results_paths["adaptive"].read_text())["rows"]
    previous_rows = dict(baseline_rows)
    previous_rows.update(adaptive_rows)
    for old_name in ("baseline_default", "noise_18", "static24", "adaptive18"):
        missing = [f"{old_name}|{c}|{r}" for c in clips for r in rates if f"{old_name}|{c}|{r}" not in previous_rows]
        if missing:
            raise RuntimeError(f"Prior measured rows lack {old_name}: {missing[:4]}")

    cache = out_dir / "cache"
    enc_dir = out_dir / "encodes"
    wav_dir = out_dir / "scoring-wav"
    for path in (cache, enc_dir, wav_dir):
        path.mkdir(parents=True, exist_ok=True)
    references = {}
    for clip in clips:
        src = source_dir / clip
        with wave.open(str(src), "rb") as w:
            if w.getframerate() != 48000 or w.getnchannels() != 2:
                raise RuntimeError(f"Expected 48 kHz stereo reference: {src}")
        ref = cache / f"ref-{clip}"
        if not ref.is_file() and not utils.wav_conv(str(src), str(ref), rate=48000, channels=2):
            raise RuntimeError(f"Cannot convert reference {src}")
        references[clip] = ref
    decoder = FDKDecoder("FDK AAC", str(FDK_BIN))
    lock = threading.Lock()

    def save_row(key, row):
        with lock:
            old_results["rows"][key] = row
            atomic_json(result_path, old_results)

    def ensure_row(label, binary, env, clip, rate, *, reuse_source=None, require_match=None):
        key = f"{label}|{clip}|{rate}"
        source = source_dir / clip
        enc = enc_dir / label / str(rate) / (Path(clip).stem + ".m4a")
        decoded = wav_dir / label / str(rate) / (Path(clip).stem + ".wav")
        enc.parent.mkdir(parents=True, exist_ok=True)
        decoded.parent.mkdir(parents=True, exist_ok=True)
        cached = old_results["rows"].get(key)
        if (cached and enc.is_file() and decoded.is_file()
                and cached.get("encoded_sha256") == sha256(enc)
                and cached.get("decoded_sha256") == sha256(decoded)):
            packet_hash = cached.get("payload_sha256") or payload_identity(enc)
            actual = cached["actual_payload_kbps"]
            profile_name = cached["profile"]
            stderr_tail = cached.get("stderr_tail", "")
        else:
            with lock:
                old_results["rows"].pop(key, None)
            enc.unlink(missing_ok=True)
            decoded.unlink(missing_ok=True)
            stderr_tail, actual = encode(binary, source, enc, rate, env, FAACEncoder)
            with tempfile.TemporaryDirectory(prefix="adaptive-noise-fdk-") as td:
                decode_and_convert(enc, decoded, decoder, utils, Path(td))
            packet_hash = payload_identity(enc)
            profile_name = profile(enc)
        pcm_hash = sha256(decoded)
        if profile_name != "HE-AAC":
            raise RuntimeError(f"Non-HE-AAC stream: {label}|{clip}|{rate} profile={profile_name!r}")
        mos, backend, mos_source = None, None, None
        cached_measurement = None
        if reuse_source:
            old_key = f"{reuse_source}|{clip}|{rate}"
            prior = previous_rows[old_key]
            prior_enc = Path(prior["encoded_path"])
            prior_wav = Path(prior["decoded_path"])
            prior_artifacts_valid = (
                prior_enc.is_file() and prior_wav.is_file()
                and prior.get("encoded_sha256") == sha256(prior_enc)
                and prior.get("decoded_sha256") == sha256(prior_wav)
            )
            prior_packet = payload_identity(prior_enc) if prior_artifacts_valid else None
            prior_pcm = sha256(prior_wav) if prior_artifacts_valid else None
            if prior_artifacts_valid and packet_hash == prior_packet and pcm_hash == prior_pcm:
                cached_measurement = prior
                mos, backend, mos_source = prior["mos"], prior.get("metric_backend", "zimtohrli"), f"identity:{old_key}"
        if require_match:
            reference = require_match
            if packet_hash != reference["payload_sha256"] or pcm_hash != reference["decoded_sha256"]:
                raise RuntimeError(f"Default control differs from {require_match['variant']} at {clip}/{rate}")
            mos, backend, mos_source = reference["mos"], reference["metric_backend"], "pcm_identity"
        if mos is None:
            mos, backend = score_wav_pair(str(references[clip]), str(decoded), mode_str="audio", sample_rate=48000)
            if mos is None or backend != "zimtohrli" or not math.isfinite(float(mos)):
                raise RuntimeError(f"Strict Zimtohrli scoring failed: {key}: {mos}, {backend}")
            mos_source = "scored"
        row = {
            "variant": label, "clip": clip, "target_kbps": rate,
            "actual_payload_kbps": actual, "profile": profile_name,
            "mos": float(mos), "metric_backend": backend, "mos_source": mos_source,
            "reuse_source_matched": cached_measurement is not None,
            "encoded_path": str(enc), "encoded_sha256": sha256(enc),
            "payload_sha256": packet_hash, "decoded_path": str(decoded),
            "decoded_sha256": pcm_hash, "stderr_tail": stderr_tail,
        }
        save_row(key, row)
        print(f"completed {key}: MOS={float(mos):.5f}, payload={actual:.3f} kbps, source={mos_source}", flush=True)
        return row

    # Fresh baseline and unset-candidate controls establish the run's baseline
    # identity. Previous baseline MOS is accepted only on packet+PCM equality.
    def run_control(clip, rate):
        old_base = previous_rows[f"baseline_default|{clip}|{rate}"]
        base = ensure_row("baseline_original", BASE_BIN, {}, clip, rate,
                          reuse_source="baseline_default")
        candidate = ensure_row("candidate_default", CAND_BIN, {}, clip, rate,
                               require_match=base)
        return base, candidate

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(run_control, clip, rate) for clip in clips for rate in rates]
        for future in as_completed(futures):
            future.result()

    def run_variant(variant, clip, rate):
        reuse_source = {
            "static18": "noise_18",
            "static24": "static24",
        }.get(variant)
        return ensure_row(variant, CAND_BIN, FEATURES[variant], clip, rate,
                          reuse_source=reuse_source)

    feature_jobs = [(v, c, r) for c in clips for r in rates for v in run_variants]
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(run_variant, *job) for job in feature_jobs]
        for future in as_completed(futures):
            future.result()

    summary = {"schema": 1, "clips": clips, "rates_kbps": rates,
               "baseline": "baseline_original", "variants": {}}
    report = ["# HE-AAC v1 limiter and static-noise stack experiment", "",
              f"Corpus: {len(clips)} clips; rates: {', '.join(str(r) for r in rates)} kbps.",
              "BD-rate is calculated with the benchmark PCHIP helper using actual packet payload bitrate and Zimtohrli MOS.", "",
              "| Variant | MOS reuse matches | Mean BD-rate (95% bootstrap CI) | Median BD-rate | Valid clips |",
              "|---|---:|---:|---:|---:|"]
    for variant in selected:
        per_clip, skipped = [], []
        reuse_matches = 0
        for clip in clips:
            points_base, points_cand = [], []
            for rate in rates:
                base = old_results["rows"][f"baseline_original|{clip}|{rate}"]
                row = old_results["rows"][f"{variant}|{clip}|{rate}"]
                reuse_matches += int(row["reuse_source_matched"])
                points_base.append((base["actual_payload_kbps"], base["mos"]))
                points_cand.append((row["actual_payload_kbps"], row["mos"]))
            value = bd_rate_curve(points_base, points_cand)
            if value is None:
                skipped.append(clip)
            else:
                per_clip.append({"clip": clip, "bd_rate_percent": value})
        vals = [x["bd_rate_percent"] for x in per_clip]
        ci = bootstrap_mean_ci(vals)
        summary["variants"][variant] = {
            "per_clip_bd_rate": per_clip, "skipped_clips": skipped,
            "mean_bd_rate_percent": statistics.mean(vals) if vals else None,
            "median_bd_rate_percent": statistics.median(vals) if vals else None,
            "mean_bd_rate_bootstrap_95_ci": {"lo": ci["lo"], "hi": ci["hi"]} if ci else None,
            "mos_reuse_identity_matches": reuse_matches,
        }
        item = summary["variants"][variant]
        mean_text = "n/a" if item["mean_bd_rate_percent"] is None else f"{item['mean_bd_rate_percent']:+.3f}%"
        ci_text = "n/a" if item["mean_bd_rate_bootstrap_95_ci"] is None else (
            f"[{item['mean_bd_rate_bootstrap_95_ci']['lo']:+.3f}%, {item['mean_bd_rate_bootstrap_95_ci']['hi']:+.3f}%]")
        median_text = "n/a" if item["median_bd_rate_percent"] is None else f"{item['median_bd_rate_percent']:+.3f}%"
        report.append(f"| {variant} | {reuse_matches} | {mean_text} {ci_text} | {median_text} | {len(vals)}/{len(clips)} |")
        item["per_rate"] = {}
        for rate in rates:
            deltas, rate_deltas, baseline_mos, variant_mos = [], [], [], []
            for clip in clips:
                base = old_results["rows"][f"baseline_original|{clip}|{rate}"]
                row = old_results["rows"][f"{variant}|{clip}|{rate}"]
                deltas.append(row["mos"] - base["mos"])
                baseline_mos.append(base["mos"])
                variant_mos.append(row["mos"])
                rate_deltas.append(100.0 * (row["actual_payload_kbps"] / base["actual_payload_kbps"] - 1.0))
            mos_ci = bootstrap_mean_ci(deltas)
            item["per_rate"][str(rate)] = {
                "mean_baseline_mos": statistics.mean(baseline_mos),
                "mean_variant_mos": statistics.mean(variant_mos),
                "mean_mos_delta": statistics.mean(deltas),
                "mos_delta_bootstrap_95_ci": {"lo": mos_ci["lo"], "hi": mos_ci["hi"]} if mos_ci else None,
                "mean_actual_rate_delta_percent": statistics.mean(rate_deltas),
            }
    atomic_json(summary_path, summary)
    report.extend(["", "## Per-rate paired deltas", "",
                   "| Variant | Rate | Baseline MOS | Variant MOS | MOS delta (95% bootstrap CI) | Mean actual bitrate delta |",
                   "|---|---:|---:|---:|---:|---:|"])
    for variant in selected:
        for rate in rates:
            item = summary["variants"][variant]["per_rate"][str(rate)]
            ci = item["mos_delta_bootstrap_95_ci"]
            ci_text = "n/a" if ci is None else f"[{ci['lo']:+.5f}, {ci['hi']:+.5f}]"
            report.append(f"| {variant} | {rate} kbps | {item['mean_baseline_mos']:.5f} | {item['mean_variant_mos']:.5f} | {item['mean_mos_delta']:+.5f} {ci_text} | {item['mean_actual_rate_delta_percent']:+.3f}% |")
    report.extend(["", "The unset candidate control must match the original baseline at both AAC packet and strict FDK-decoded PCM level. Cached MOS is reused only after packet and PCM identity.", "", "The limiter is independently opt-in; static noise defaults to level 12 and the tested overrides are 18 and 24.", "", "Size and speed remain untested. These are experiment results, not a shipping decision.", ""])

    # Direct pairwise comparisons expose both baseline-relative quality and
    # whether adding adaptive noise changes the invf-only result.
    pairs = [(name, "baseline_original") for name in selected]
    for name in selected:
        if name == "stack18": pairs.append((name, "static18"))
        if name == "stack24": pairs.append((name, "static24"))
        if name.startswith("stack"): pairs.append((name, "limiter"))
    direct = {"clips": len(clips), "rates_kbps": rates, "comparisons": {}}
    for cand_name, ref_name in pairs:
        if cand_name not in selected or (ref_name not in run_variants and ref_name != "baseline_original"):
            continue
        per_clip, packet_ties, pcm_ties = [], 0, 0
        for clip in clips:
            ref_points, cand_points = [], []
            for rate in rates:
                ref = old_results["rows"][f"{ref_name}|{clip}|{rate}"]
                cand = old_results["rows"][f"{cand_name}|{clip}|{rate}"]
                ref_points.append((ref["actual_payload_kbps"], ref["mos"]))
                cand_points.append((cand["actual_payload_kbps"], cand["mos"]))
                packet_ties += ref["payload_sha256"] == cand["payload_sha256"]
                pcm_ties += ref["decoded_sha256"] == cand["decoded_sha256"]
            value = bd_rate_curve(ref_points, cand_points)
            if value is not None:
                per_clip.append({"clip": clip, "bd_rate_percent": value})
        vals = [x["bd_rate_percent"] for x in per_clip]
        bd_ci = bootstrap_mean_ci(vals)
        per_rate = {}
        for rate in rates:
            mos_delta, bitrate_delta = [], []
            for clip in clips:
                ref = old_results["rows"][f"{ref_name}|{clip}|{rate}"]
                cand = old_results["rows"][f"{cand_name}|{clip}|{rate}"]
                mos_delta.append(cand["mos"] - ref["mos"])
                bitrate_delta.append(100 * (cand["actual_payload_kbps"] / ref["actual_payload_kbps"] - 1))
            ci = bootstrap_mean_ci(mos_delta)
            per_rate[str(rate)] = {
                "mean_reference_mos": statistics.mean(old_results["rows"][f"{ref_name}|{clip}|{rate}"]["mos"] for clip in clips),
                "mean_candidate_mos": statistics.mean(old_results["rows"][f"{cand_name}|{clip}|{rate}"]["mos"] for clip in clips),
                "mean_mos_delta": statistics.mean(mos_delta),
                "mos_delta_bootstrap_95_ci": ci,
                "mean_actual_bitrate_delta_percent": statistics.mean(bitrate_delta),
            }
        direct["comparisons"][f"{cand_name}_vs_{ref_name}"] = {
            "mean_bd_rate_percent": statistics.mean(vals) if vals else None,
            "median_bd_rate_percent": statistics.median(vals) if vals else None,
            "mean_bd_rate_bootstrap_95_ci": bd_ci, "per_clip_bd_rate": per_clip,
            "per_rate": per_rate, "valid_clips": len(vals),
            "aac_packet_identity_ties": packet_ties, "decoded_pcm_identity_ties": pcm_ties,
            "rows": len(clips) * len(rates),
        }
    direct_path = out_dir / "direct-comparisons.json"
    atomic_json(direct_path, direct)
    report.extend(["", "## Direct comparisons", "",
                   "Negative BD-rate favors the named candidate; intervals bootstrap over clips.", "",
                   "| Candidate vs reference | Mean BD-rate (95% CI) | Median | Packet ties | PCM ties |", "|---|---:|---:|---:|---:|"])
    for pair_name, item in direct["comparisons"].items():
        ci = item["mean_bd_rate_bootstrap_95_ci"]
        ci_text = "n/a" if ci is None else f"[{ci['lo']:+.3f}%, {ci['hi']:+.3f}%]"
        report.append(f"| {pair_name} | {item['mean_bd_rate_percent']:+.3f}% {ci_text} | {item['median_bd_rate_percent']:+.3f}% | {item['aac_packet_identity_ties']}/{item['rows']} | {item['decoded_pcm_identity_ties']}/{item['rows']} |")
    report.extend(["", "Per-rate paired MOS deltas and actual payload-rate deltas are machine-readable in `direct-comparisons.json`.", ""])
    summary_md_path.write_text("\n".join(report))
    print(f"Saved rows: {result_path}")
    print(f"Saved summary: {summary_path}")
    return 0


if __name__ == "__main__":
    os.environ.setdefault("NUMBA_DISABLE_JIT", "1")
    raise SystemExit(main())
