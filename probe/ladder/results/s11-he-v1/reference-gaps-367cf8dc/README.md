# Raw reference-gap diagnostic (FAAC 367cf8dc)

This folder is a portable, metadata-only snapshot of a diagnostic over the benchmark matrix that already existed when FAAC `367cf8dc5e76085b02668d3b212fe6576001ee07` was tested. It contains no audio, encoded streams, decoder binaries, or reference-encoder source. The two original JSON matrices are preserved as deterministic gzip files under `raw/`; their uncompressed and compressed SHA-256 values are in `manifest.json`. A small set of exact Apple reference streams needed for cloud-side reproduction is packaged separately at [`probe/ladder/ref/apple-s11-367cf8dc`](../../../ref/apple-s11-367cf8dc); it is not part of this metadata folder.

## Contents and reproduction

- `raw/comparison_results.json.gz`: all 14,971 encoder result rows.
- `raw/comparison_results.json.decoders.json.gz`: decoder MOS and robustness sidecar.
- `analysis/*-stream-validation.json`: stream-header checks for the focused 48 kHz stereo rows (588 Apple rows; 882 FAAC/FDK rows).
- `analysis/equal-rate-gaps.json`: per-clip results and focused BD-rate summaries.
- `analysis/RAW-REFERENCE-AUDIT.md`: matrix inventory, invalid-row accounting, sample-rate correction, and provenance limits.
- `SUGGESTED_EXPERIMENTS.md`: proposed follow-up only; none of it has been run.
- `analysis/analyze.py`: the original analysis script, retained byte-for-byte for provenance. It refers to the original local benchmark checkout and prints an error when a CI is null after it has already written its JSON output. Use the portable command below for future reproduction.

The source benchmark checkout used by the original analysis was `faac-benchmark` at HEAD `bdce18f7123f51c31795afe9fb4571c18f507a77`. Its decoder and utility files were dirty and a run-time decoder/helper binary hash was not recorded with the matrix. Therefore this package reproduces the recorded analysis inputs and metadata; it cannot establish the exact historical helper or decoder binary provenance. Do not present this as a fresh run or as a result for PR70.

To decompress and verify the raw inputs from this directory:

```sh
python3 - <<'PY'
import gzip, hashlib, json
from pathlib import Path
p = Path('.')
m = json.loads((p / 'manifest.json').read_text())
for name, row in m['raw_inputs'].items():
    data = gzip.decompress((p / row['compressed_path']).read_bytes())
    assert hashlib.sha256(data).hexdigest() == row['original_sha256']
    assert len(data) == row['original_bytes']
print('raw matrix hashes verified')
PY
```

The historical analysis script requires the sibling benchmark checkout and its Python dependencies; its hard-coded path is intentionally preserved rather than silently rewritten. A portable focused re-derivation is available as `analysis/derive_focused_bd.py`; it takes `--bench-root`, reads and hash-verifies the packaged matrix, and writes its output outside the benchmark checkout. Example from this directory: `../faac-benchmark/.venv/bin/python analysis/derive_focused_bd.py --bench-root ../faac-benchmark --output /tmp/focused-bd.json`. The JSON results and validation records here are sufficient to inspect the recorded findings without that checkout. No benchmark rows were changed or filtered in the archived raw matrices.

## Sample-rate correction and focused result

The original broad LC comparison pooled Apple output streams at different sample rates. Apple LC `48k_stereo_32k` and `48k_stereo_48k` scenarios produced 12 kHz and 24 kHz streams, while the 96 kbit/s scenario produced 48 kHz. Those rows cannot support a single 48 kHz LC comparison. The corrected comparison uses only actual 48 kHz stereo stream headers, and only the matched rate ladders described in `RAW-REFERENCE-AUDIT.md`. Positive BD-rate means FAAC requires more bits for equal decoded MOS.

These are retrospective benchmark diagnostics, not listening-test conclusions. The focus result does not establish why the quality gap exists or imply that a new encoder change will close it. See `SUGGESTED_EXPERIMENTS.md` for a bounded proposal and its controls.
