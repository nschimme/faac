#!/usr/bin/env python3
"""LC identity and HE-AAC strict-decoder boundary gates for tier17 builds."""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import math
import os
import struct
import sys
import tempfile
import threading
import wave
from datetime import datetime, timezone
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import run_envcount as envcount

BASE_REF = envcount.BASE_BIN
STACK_REF = envcount.STACK_REF_BIN
TIER_BASE = Path("/private/tmp/faac-envcount-tier17-base/build-tier17/frontend/faac")
TIER_STACK = Path("/private/tmp/faac-envcount-tier17-stack/build-tier17/frontend/faac")
LC_VARIANTS = {
    "lc_base_reference": (BASE_REF, None),
    "lc_tier17_base": (TIER_BASE, "lc_base_reference"),
    "lc_stack_reference": (STACK_REF, None),
    "lc_tier17_stack": (TIER_STACK, "lc_stack_reference"),
}
HE_VARIANTS = {"he_tier17_base": TIER_BASE, "he_tier17_stack": TIER_STACK}
RATES = (32, 48, 64, 96)


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def write_fixture(path: Path, channels: int, frames: int, kind: str) -> None:
    rate = 48000
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(2)
        w.setframerate(rate)
        buf = bytearray()
        for i in range(frames):
            t = i / rate
            for ch in range(channels):
                if kind == "silence":
                    value = 0.0
                else:
                    phase = ch * 0.31
                    value = 0.36 * math.sin(2.0 * math.pi * 997.0 * t + phase)
                    value += 0.18 * math.sin(2.0 * math.pi * 14873.0 * t + 1.7 * phase)
                    if kind == "partial" and i >= frames - 700:
                        value += 0.27 * math.sin(2.0 * math.pi * 7349.0 * t)
                value = max(-0.98, min(0.98, value))
                buf += struct.pack("<h", int(round(value * 32767.0)))
        w.writeframes(buf)


def atomic_json(path: Path, obj) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(obj, indent=2, sort_keys=True) + "\n")
    os.replace(tmp, path)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--output-dir", type=Path, default=envcount.DEFAULT_OUTPUT_ROOT / "tier17/production-gates")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    sys.path.insert(0, str(envcount.BENCH))
    sys.path.insert(0, str(envcount.BENCH / "scripts"))
    from codec_bench.decoders import FDKDecoder
    from codec_bench.encoders import FAACEncoder

    sources = envcount.BENCH / "data/external/audio"
    clips = ("sandman.16b48k.wav", "velvet.16b48k.wav", "21-classic.441.16b48k.wav", "fms.wav")
    for clip in clips:
        with wave.open(str(sources / clip), "rb") as w:
            if w.getframerate() != 48000 or w.getnchannels() != 2:
                raise RuntimeError(f"Expected 48 kHz stereo clip: {clip}")

    out = args.output_dir
    fixtures = out / "fixtures"
    fixture_specs = {
        "silence_mono": (1, 48000, "silence"),
        "silence_stereo": (2, 48000, "silence"),
        "tone_mono": (1, 96000, "tone"),
        "tone_stereo": (2, 96000, "tone"),
        "partial_mono": (1, 11777, "partial"),
        "partial_stereo": (2, 11777, "partial"),
    }
    for name, (channels, frames, kind) in fixture_specs.items():
        path = fixtures / f"{name}.wav"
        if not path.exists():
            write_fixture(path, channels, frames, kind)

    controls = {}
    for name, (binary, expected) in LC_VARIANTS.items():
        controls[name] = {"binary": str(binary), "sha256": envcount.sha256(binary), "role": "LC identity", "expected_reference": expected}
    for name, binary in HE_VARIANTS.items():
        controls[name] = {"binary": str(binary), "sha256": envcount.sha256(binary), "role": "HE strict-decode boundary"}
    config = {
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "runner_sha256": sha256(Path(__file__).resolve()),
        "baseline_source_head": envcount.git_head(Path("/private/tmp/faac-envcount-tier17-base")),
        "stack_source_head": envcount.git_head(Path("/private/tmp/faac-envcount-tier17-stack")),
        "controls": controls,
        "lc_clips": list(clips), "lc_rates_kbps": list(RATES),
        "he_fixture_specs": {name: {"channels": ch, "frames": frames, "kind": kind, "sha256": sha256(fixtures / f"{name}.wav")}
                             for name, (ch, frames, kind) in fixture_specs.items()},
        "he_cbr_cases": {
            "tone_mono": [16, 17, 18], "tone_stereo": [32, 34, 36],
            "silence_mono": [16, 17, 18], "silence_stereo": [32, 34, 36],
            "partial_mono": [17, 32], "partial_stereo": [34, 64],
        },
        "strict_decoders": ["FDK AAC", "FAAD3 --strict", "FFmpeg"],
        "scoring": "No MOS scoring; verify ordered AAC packet+FDK PCM identity for LC, and strict decodability for HE edge fixtures.",
    }
    config_path = out / "config.json"
    if config_path.exists():
        old = json.loads(config_path.read_text())
        for key in config:
            if key != "created_utc" and old.get(key) != config[key]:
                raise RuntimeError(f"Immutable production-gate config changed: {key}")
    else:
        atomic_json(config_path, config)

    result_path = out / "results.json"
    results = json.loads(result_path.read_text()) if result_path.exists() else {"schema": 1, "rows": {}}
    rows = results["rows"]
    lock = threading.Lock()
    dec = FDKDecoder("FDK AAC", str(envcount.FDK_BIN))
    encoder_variants = {**{k: v[0] for k, v in LC_VARIANTS.items()}, **HE_VARIANTS}
    jobs = []
    for variant in LC_VARIANTS:
        for clip in clips:
            for rate in RATES:
                jobs.append(("lc", variant, clip, rate, Path(clip)))
    for variant in HE_VARIANTS:
        for fixture in fixture_specs:
            channels, _, _ = fixture_specs[fixture]
            rates = config["he_cbr_cases"][fixture.replace("_", "_")]
            for rate in rates:
                jobs.append(("he", variant, fixture, rate, Path(f"{fixture}.wav")))

    def process(mode: str, variant: str, label: str, rate: int, source_name: Path) -> dict:
        binary = encoder_variants[variant]
        source = (sources / source_name) if mode == "lc" else (fixtures / source_name)
        key = f"{variant}|{label}|{rate}"
        encoded = out / "encodes" / variant / str(rate) / (Path(label).stem + ".m4a")
        decoded = out / "decoded-fdk" / variant / str(rate) / (Path(label).stem + ".wav")
        encoded.parent.mkdir(parents=True, exist_ok=True)
        decoded.parent.mkdir(parents=True, exist_ok=True)
        cached = rows.get(key)
        if not (cached and encoded.is_file() and decoded.is_file()
                and cached.get("encoded_sha256") == sha256(encoded)
                and cached.get("decoded_sha256") == sha256(decoded)):
            encoded.unlink(missing_ok=True)
            decoded.unlink(missing_ok=True)
            channels = wave.open(str(source), "rb").getnchannels()
            encoder = FAACEncoder("faac-tier17-production-gate", str(binary), profile=("lc" if mode == "lc" else "he"))
            cmd = encoder.get_encode_cmd(str(source), str(encoded), rate, channels, 48000,
                                         rate_control=("cbr" if mode == "he" else "abr"))
            required_object = "lc" if mode == "lc" else "he-aac-v1"
            if "--object-type" not in cmd or cmd[cmd.index("--object-type") + 1] != required_object:
                raise RuntimeError(f"Wrong forced object type for {key}: {cmd}")
            if mode == "he" and "--cbr" not in cmd:
                raise RuntimeError(f"CBR flag missing for boundary case {key}")
            clean_env = {k: v for k, v in os.environ.items() if not k.startswith("FAAC_SBR_")}
            envcount.run(cmd, env=clean_env)
            actual_profile = envcount.profile(encoded)
            wanted = "HE-AAC" if mode == "he" else "LC"
            if actual_profile != wanted:
                raise RuntimeError(f"Profile mismatch {key}: {actual_profile} != {wanted}")
            with tempfile.TemporaryDirectory(prefix="tier17-fdk-") as tmp:
                envcount.decode_fdk(encoded, decoded, dec, utils, Path(tmp))
            packet_hash = envcount.payload_identity(encoded)
            pcm_hash = sha256(decoded)
            encoded_hash = sha256(encoded)
            decoded_hash = pcm_hash
        else:
            actual_profile = cached["profile"]
            packet_hash = cached["payload_sha256"]
            pcm_hash = cached["decoded_pcm_sha256"]
            encoded_hash = cached["encoded_sha256"]
            decoded_hash = cached["decoded_sha256"]

        ref_key = None
        packet_match = pcm_match = None
        if mode == "lc" and LC_VARIANTS[variant][1]:
            ref_variant = LC_VARIANTS[variant][1]
            ref_key = f"{ref_variant}|{label}|{rate}"
            ref_path = out / "encodes" / ref_variant / str(rate) / (Path(label).stem + ".m4a")
            ref_decoded = out / "decoded-fdk" / ref_variant / str(rate) / (Path(label).stem + ".wav")
            packet_match = packet_hash == envcount.payload_identity(ref_path)
            pcm_match = pcm_hash == sha256(ref_decoded)

        faad_wav = out / "decoded-faad" / variant / str(rate) / (Path(label).stem + ".wav")
        faad_wav.parent.mkdir(parents=True, exist_ok=True)
        faad = envcount.run([str(envcount.FAAD_BIN), "--strict", "--quiet", "--overwrite", "-o", str(faad_wav), str(encoded)])
        ffmpeg = envcount.run([envcount.FFMPEG, "-v", "error", "-y", "-i", str(encoded), "-f", "null", "-"])
        row = {
            "key": key, "mode": mode, "variant": variant, "source": str(source), "rate_kbps": rate,
            "channels": channels, "profile": actual_profile, "payload_sha256": packet_hash,
            "decoded_pcm_sha256": pcm_hash, "encoded_sha256": encoded_hash, "decoded_sha256": decoded_hash,
            "encoded_path": str(encoded), "decoded_path": str(decoded),
            "fdk_pass": True, "faad_pass": faad.returncode == 0 and faad_wav.is_file() and faad_wav.stat().st_size > 0,
            "ffmpeg_pass": ffmpeg.returncode == 0,
            "lc_reference": ref_key, "lc_packet_match": packet_match, "lc_pcm_match": pcm_match,
        }
        with lock:
            rows[key] = row
            atomic_json(result_path, results)
        if not row["faad_pass"] or not row["ffmpeg_pass"] or (mode == "lc" and variant.startswith("lc_tier17") and not (packet_match and pcm_match)):
            raise RuntimeError(f"Production gate failed: {key}: {row}")
        print(f"completed {key}: FDK/FAAD/FFmpeg pass; LC match={packet_match}/{pcm_match}", flush=True)
        return row

    import utils
    # Produce each LC reference matrix before its tier17 candidate matrix so
    # a fast candidate worker cannot race the reference file creation.
    phases = [
        [job for job in jobs if job[0] == "lc" and job[1] in ("lc_base_reference", "lc_stack_reference")],
        [job for job in jobs if job[0] == "lc" and job[1] in ("lc_tier17_base", "lc_tier17_stack")],
        [job for job in jobs if job[0] == "he"],
    ]
    for phase in phases:
        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            futures = [pool.submit(process, *job) for job in phase]
            for future in as_completed(futures):
                future.result()

    lc_rows = [row for row in rows.values() if row["mode"] == "lc"]
    he_rows = [row for row in rows.values() if row["mode"] == "he"]
    summary = {
        "schema": 1,
        "lc": {"rows": len(lc_rows), "base_packet_matches": sum(r["lc_packet_match"] for r in lc_rows if r["variant"] == "lc_tier17_base"),
               "base_pcm_matches": sum(r["lc_pcm_match"] for r in lc_rows if r["variant"] == "lc_tier17_base"),
               "stack_packet_matches": sum(r["lc_packet_match"] for r in lc_rows if r["variant"] == "lc_tier17_stack"),
               "stack_pcm_matches": sum(r["lc_pcm_match"] for r in lc_rows if r["variant"] == "lc_tier17_stack")},
        "he": {"rows": len(he_rows), "fdk_pass": sum(r["fdk_pass"] for r in he_rows),
               "faad_pass": sum(r["faad_pass"] for r in he_rows), "ffmpeg_pass": sum(r["ffmpeg_pass"] for r in he_rows),
               "rates_and_boundary_cases": config["he_cbr_cases"]},
    }
    atomic_json(out / "summary.json", summary)
    print(json.dumps(summary, indent=2, sort_keys=True), flush=True)
    if summary["lc"]["base_packet_matches"] != 16 or summary["lc"]["base_pcm_matches"] != 16 or summary["lc"]["stack_packet_matches"] != 16 or summary["lc"]["stack_pcm_matches"] != 16:
        raise RuntimeError("LC identity matrix did not match exactly")
    if summary["he"]["fdk_pass"] != len(he_rows) or summary["he"]["faad_pass"] != len(he_rows) or summary["he"]["ffmpeg_pass"] != len(he_rows):
        raise RuntimeError("HE strict decoder matrix did not pass")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
