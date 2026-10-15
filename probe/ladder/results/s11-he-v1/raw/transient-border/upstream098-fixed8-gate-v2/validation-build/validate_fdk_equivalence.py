#!/usr/bin/env python3
"""Recheck the isolated fdkdec build against a frozen scored row matrix.

Run with ../faac-benchmark/.venv/bin/python. Outputs are written under
/private/tmp/faac-upstream098-fdk-ab/full392.
"""
from __future__ import annotations

import hashlib
import json
import subprocess
import sys
import wave
from pathlib import Path

BENCH = Path("/Users/nschimme/gitprojects/faac-benchmark")
ROWS = Path("/Users/nschimme/gitprojects/faac/.cache/he-v1-feature-study/transient-border/upstream098-fixed8-gate-v2/full49/rows.json")
OLD = Path("/private/tmp/faac-he-v1-upstream098-run-provenance/fdkdec-at-capture")
NEW = Path("/private/tmp/faac-upstream098-fdk-new/fdkdec")
OUT = Path("/private/tmp/faac-upstream098-fdk-ab/full392")


def sha(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def wav_info(path: Path) -> dict:
    with wave.open(str(path), "rb") as wav:
        params = wav.getparams()
        pcm = wav.readframes(params.nframes)
    return {
        "frames": params.nframes,
        "channels": params.nchannels,
        "sample_width": params.sampwidth,
        "rate": params.framerate,
        "pcm_sha256": hashlib.sha256(pcm).hexdigest(),
        "file_sha256": sha(path),
    }


def main() -> int:
    sys.path.insert(0, str(BENCH))
    from utils import wav_conv

    data = json.loads(ROWS.read_text())
    OUT.mkdir(parents=True, exist_ok=True)
    results, errors = [], []
    for key, row in sorted(data["rows"].items()):
        encoded = Path(row["encoded_path"])
        prior = Path(row["decoded_path"])
        if sha(encoded) != row["encoded_sha256"]:
            errors.append({"key": key, "stage": "encoded hash"})
            continue
        decoded = OUT / (key.replace("|", "_") + ".wav")
        normalized_new = OUT / (key.replace("|", "_") + "_newnorm.wav")
        normalized_prior = OUT / (key.replace("|", "_") + "_oldnorm.wav")
        proc = subprocess.run([str(NEW), str(encoded), str(decoded)], capture_output=True, text=True)
        if proc.returncode or not decoded.is_file():
            errors.append({"key": key, "stage": "decode", "returncode": proc.returncode})
            continue
        if not wav_conv(str(decoded), str(normalized_new), rate=48000, channels=2):
            errors.append({"key": key, "stage": "normalize new"})
            continue
        if not wav_conv(str(prior), str(normalized_prior), rate=48000, channels=2):
            errors.append({"key": key, "stage": "normalize prior"})
            continue
        new = wav_info(normalized_new)
        old = wav_info(normalized_prior)
        shape = ("frames", "channels", "sample_width", "rate")
        results.append({
            "key": key,
            "encoded_sha256": row["encoded_sha256"],
            "prior_normalized_pcm_sha256": old["pcm_sha256"],
            "new_normalized_pcm_sha256": new["pcm_sha256"],
            "prior_shape": {field: old[field] for field in shape},
            "new_shape": {field: new[field] for field in shape},
            "normalized_pcm_equal": old["pcm_sha256"] == new["pcm_sha256"]
            and all(old[field] == new[field] for field in shape),
            "new_raw_wav_sha256": sha(decoded),
            "fdk_stdout": proc.stdout.strip(),
        })
    summary = {
        "rows_input": len(data["rows"]),
        "rows_checked": len(results),
        "normalized_pcm_equal": sum(row["normalized_pcm_equal"] for row in results),
        "errors": errors,
        "old_decoder_sha256": sha(OLD),
        "new_decoder_sha256": sha(NEW),
        "rows_sha256": sha(ROWS),
        "rows_path": str(ROWS),
    }
    result_path = OUT / "full392-normalized-equivalence.json"
    result_path.write_text(json.dumps({"schema": 1, "summary": summary, "rows": results}, indent=2, sort_keys=True) + "\n")
    print(json.dumps(summary, indent=2))
    print(result_path)
    return 0 if not errors and len(results) == len(data["rows"]) and summary["normalized_pcm_equal"] == len(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
