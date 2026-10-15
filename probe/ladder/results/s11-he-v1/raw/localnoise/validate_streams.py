#!/usr/bin/env python3
"""Strictly validate encoded streams with FAAD and ffmpeg.

This checks decoder acceptance only; it does not compare decoded PCM output.
"""

import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile


FAAD = Path("/private/tmp/faac-he-v1-decoder-build/frontend/faad")
FFMPEG = "ffmpeg"
DECODER_COMMIT = "333fe311"


def sha256_file(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run_decoder(command, reject_stderr=False):
    try:
        completed = subprocess.run(command, capture_output=True, text=True, check=False)
    except OSError as error:
        return {
            "passed": False,
            "returncode": None,
            "stderr": str(error),
            "stdout": "",
        }
    stderr = completed.stderr or ""
    passed = completed.returncode == 0 and (not reject_stderr or not stderr)
    return {
        "passed": passed,
        "returncode": completed.returncode,
        "stderr": stderr,
        "stdout": completed.stdout or "",
    }


def validate_stream(stream):
    encoded = Path(stream["encoded_path"])
    row = {
        "encoded_path": str(encoded),
        "variants": sorted(stream["variants"]),
        "encoded_sha256": sha256_file(encoded),
    }

    fd, wav_name = tempfile.mkstemp(suffix=".wav")
    os.close(fd)
    wav_path = Path(wav_name)
    try:
        row["faad"] = run_decoder(
            [str(FAAD), "--strict", "--overwrite", "-o", str(wav_path), str(encoded)]
        )
    finally:
        wav_path.unlink(missing_ok=True)

    row["ffmpeg"] = run_decoder(
        [FFMPEG, "-v", "error", "-xerror", "-i", str(encoded), "-f", "null", "-"],
        reject_stderr=True,
    )
    return row


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results_json", type=Path, help="feature results JSON")
    parser.add_argument(
        "--variant",
        help="comma-separated variant names to validate (default: all variants)",
    )
    args = parser.parse_args()

    with args.results_json.open(encoding="utf-8") as source:
        results = json.load(source)
    rows = results["rows"]
    if isinstance(rows, dict):
        rows = rows.values()
    selected = set(args.variant.split(",")) if args.variant else None

    streams = {}
    for row in rows:
        variant = row["variant"]
        if selected is not None and variant not in selected:
            continue
        encoded_path = str(Path(row["encoded_path"]).resolve())
        entry = streams.setdefault(encoded_path, {"encoded_path": encoded_path, "variants": set()})
        entry["variants"].add(variant)

    if not streams:
        parser.error("no encoded streams matched the selected variants")
    if not FAAD.is_file():
        parser.error(f"FAAD decoder not found: {FAAD}")

    decoder_sha256 = sha256_file(FAAD)
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        stream_results = list(pool.map(validate_stream, streams.values()))

    counts = {
        "streams": len(stream_results),
        "faad_pass": sum(item["faad"]["passed"] for item in stream_results),
        "faad_fail": sum(not item["faad"]["passed"] for item in stream_results),
        "ffmpeg_pass": sum(item["ffmpeg"]["passed"] for item in stream_results),
        "ffmpeg_fail": sum(not item["ffmpeg"]["passed"] for item in stream_results),
    }
    output = {
        "schema": 1,
        "results_json": str(args.results_json.resolve()),
        "decoder_commit": DECODER_COMMIT,
        "original_current_decoder_commit": DECODER_COMMIT,
        "faad_binary": str(FAAD),
        "faad_binary_sha256": decoder_sha256,
        "ffmpeg_binary": "ffmpeg (resolved through PATH)",
        "selected_variants": sorted(selected) if selected is not None else None,
        "counts": counts,
        "streams": stream_results,
    }
    output_path = args.results_json.resolve().with_name("interop-results.json")
    output_path.write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {output_path}")
    print(json.dumps(counts, sort_keys=True))
    return 1 if counts["faad_fail"] or counts["ffmpeg_fail"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
