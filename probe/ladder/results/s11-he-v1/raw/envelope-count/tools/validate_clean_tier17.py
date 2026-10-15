#!/usr/bin/env python3
"""Validate the clean tier17 stack candidate against matched hybrid streams."""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import json
import os
import sys
import tempfile
import threading
from datetime import datetime, timezone
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import run_envcount as envcount

BENCH = envcount.BENCH
FULL_RESULTS = envcount.DEFAULT_OUTPUT_ROOT / "full-active-lambda" / "envcount-results.json"
CONTROLS = {
    "tier17_clean_stack": (Path("/private/tmp/faac-he-v1-envelope-production/build-tier17/frontend/faac"), "all_stack", "stack_reference"),
}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--control", action="append", choices=tuple(CONTROLS),
                        help="control to validate; repeat for multiple (default: all)")
    parser.add_argument("--output-dir", type=Path, default=HERE.parent / "production-final/identity-clean")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    controls = {name: CONTROLS[name] for name in (args.control or list(CONTROLS))}

    sys.path.insert(0, str(BENCH))
    sys.path.insert(0, str(BENCH / "scripts"))
    import utils
    from codec_bench.decoders import FDKDecoder
    from codec_bench.encoders import FAACEncoder

    full_doc = json.loads(FULL_RESULTS.read_text())
    full_rows = full_doc["rows"]
    clips = sorted(path.name for path in (BENCH / "data/external/audio").glob("*.wav"))
    rates = (32, 48, 64, 96)
    if len(clips) != 49:
        raise RuntimeError(f"Expected 49 benchmark clips, got {len(clips)}")
    for name, (binary, _, _) in controls.items():
        if not binary.is_file():
            raise FileNotFoundError(f"{name}: {binary}")
    identity = {
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "runner_sha256": envcount.sha256(Path(__file__).resolve()),
        "probe_results_sha256": envcount.sha256(FULL_RESULTS),
        "clips": clips,
        "rates_kbps": list(rates),
        "controls": {
            name: {"binary": str(binary), "binary_sha256": envcount.sha256(binary),
                   "library": str(binary.parents[1] / "libfaac/libfaac.2.dylib"),
                   "library_sha256": envcount.sha256(binary.parents[1] / "libfaac/libfaac.2.dylib"),
                   "high_rate_probe_variant": variant, "low_rate_reference_variant": context, "low_rate_cutoff_kbps": 32}
            for name, (binary, variant, context) in controls.items()
        },
        "validation": "ordered AAC packet size+sha256 identity and strict FDK decoded WAV sha256; no MOS scoring",
    }
    out = args.output_dir
    out.mkdir(parents=True, exist_ok=True)
    config_path = out / "config.json"
    if config_path.exists():
        prior = json.loads(config_path.read_text())
        for key in ("runner_sha256", "probe_results_sha256", "clips", "rates_kbps", "controls", "validation"):
            if prior.get(key) != identity[key]:
                raise RuntimeError(f"Immutable config mismatch: {key}")
    else:
        tmp = config_path.with_suffix(".json.tmp")
        tmp.write_text(json.dumps(identity, indent=2, sort_keys=True) + "\n")
        os.replace(tmp, config_path)

    result_path = out / "equivalence-results.json"
    results = json.loads(result_path.read_text()) if result_path.exists() else {"schema": 1, "rows": {}}
    rows = results["rows"]
    lock = threading.Lock()
    decoder = FDKDecoder("FDK AAC", str(envcount.FDK_BIN))
    source_dir = BENCH / "data/external/audio"
    encode_dir, decode_dir = out / "encodes", out / "decoded"

    def process(control: str, clip: str, rate: int) -> dict:
        binary, variant, context = CONTROLS[control]
        key = f"{control}|{clip}|{rate}"
        expected_variant = context if rate == 32 else variant
        expected = full_rows[f"{expected_variant}|{clip}|{rate}"]
        encoded = encode_dir / control / str(rate) / (Path(clip).stem + ".m4a")
        decoded = decode_dir / control / str(rate) / (Path(clip).stem + ".wav")
        encoded.parent.mkdir(parents=True, exist_ok=True)
        decoded.parent.mkdir(parents=True, exist_ok=True)
        cached = rows.get(key)
        if not (cached and encoded.is_file() and decoded.is_file()
                and cached.get("encoded_sha256") == envcount.sha256(encoded)
                and cached.get("decoded_sha256") == envcount.sha256(decoded)):
            encoded.unlink(missing_ok=True)
            decoded.unlink(missing_ok=True)
            source = source_dir / clip
            encoder = FAACEncoder("faac-clean-tier17-control", str(binary), profile="he")
            command = encoder.get_encode_cmd(str(source), str(encoded), rate, 2, 48000)
            if "--object-type" not in command or command[command.index("--object-type") + 1] != "he-aac-v1":
                raise RuntimeError(f"HE-AAC v1 not forced for {key}: {command}")
            clean_env = {k: v for k, v in os.environ.items() if not k.startswith("FAAC_SBR_")}
            envcount.run(command, env=clean_env)
            profile = envcount.profile(encoded)
            if profile != "HE-AAC":
                raise RuntimeError(f"Unexpected stream profile for {key}: {profile}")
            with tempfile.TemporaryDirectory(prefix="envcount-prod-fdk-") as temp:
                envcount.decode_fdk(encoded, decoded, decoder, utils, Path(temp))
            packet_hash = envcount.payload_identity(encoded)
            pcm_hash = envcount.sha256(decoded)
            encoded_hash = envcount.sha256(encoded)
            decoded_hash = envcount.sha256(decoded)
        else:
            profile = cached["profile"]
            packet_hash = cached["payload_sha256"]
            pcm_hash = cached["decoded_pcm_sha256"]
            encoded_hash = cached["encoded_sha256"]
            decoded_hash = cached["decoded_sha256"]
        expected_encoded = Path(expected["encoded_path"])
        expected_decoded = Path(expected["decoded_path"])
        packet_equal = packet_hash == envcount.payload_identity(expected_encoded)
        pcm_equal = pcm_hash == envcount.sha256(expected_decoded)
        row = {
            "key": key, "control": control, "clip": clip, "target_kbps": rate,
            "probe_variant": expected_variant, "reference_context": context, "profile": profile,
            "payload_sha256": packet_hash, "expected_payload_sha256": expected.get("payload_sha256"),
            "decoded_pcm_sha256": pcm_hash, "expected_decoded_pcm_sha256": expected.get("decoded_sha256"),
            "packet_identity_match": packet_equal, "pcm_identity_match": pcm_equal,
            "exact_match": packet_equal and pcm_equal,
            "encoded_path": str(encoded), "encoded_sha256": encoded_hash,
            "decoded_path": str(decoded), "decoded_sha256": decoded_hash,
        }
        with lock:
            rows[key] = row
            tmp = result_path.with_suffix(".json.tmp")
            tmp.write_text(json.dumps(results, indent=2, sort_keys=True) + "\n")
            os.replace(tmp, result_path)
        print(f"completed {key}: packet={packet_equal} pcm={pcm_equal}", flush=True)
        return row

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(process, control, clip, rate)
                   for control in controls for clip in clips for rate in rates]
        for future in as_completed(futures):
            future.result()

    counts = {}
    for control in controls:
        subset = [row for row in rows.values() if row["control"] == control]
        counts[control] = {
            "rows": len(subset), "packet_identity_matches": sum(row["packet_identity_match"] for row in subset),
            "pcm_identity_matches": sum(row["pcm_identity_match"] for row in subset),
            "exact_matches": sum(row["exact_match"] for row in subset),
        }
    summary = {"schema": 1, "rows": len(rows), "controls": counts,
               "all_exact": len(rows) == len(controls) * len(clips) * len(rates)
               and all(item["exact_matches"] == item["rows"] for item in counts.values())}
    tmp = (out / "equivalence-summary.json.tmp")
    tmp.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")
    os.replace(tmp, out / "equivalence-summary.json")
    print(json.dumps(summary, indent=2, sort_keys=True), flush=True)
    if not summary["all_exact"]:
        raise RuntimeError("One or more clean tier17 streams differ from the matched hybrid packet/PCM reference")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
