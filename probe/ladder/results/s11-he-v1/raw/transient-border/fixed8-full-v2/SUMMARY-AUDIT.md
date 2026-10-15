# Fixed-8 full summary audit

The full raw matrix contains 784 rows: 49 clips × four rates × two fixed-8 candidates, plus two matched references for each clip/rate. All 784 encoded streams are present in the interop results and passed both strict FAAD3 and FFmpeg.

The runner's imported four-clip summary helper initially retained the gate clip list. The first `summary.json` was therefore superseded after recomputing PCHIP and clip-bootstrap statistics from the unchanged 49-clip `rows.json`, explicitly setting the clip list from all feature rows. The corrected `summary.json` reports `clips` for all 49 clips; its source rows, encoder outputs, MOS values, and interop records were not changed. `adaptive-vs-fixed8.json` was computed independently from the 49 unique raw clip names.

The standalone derivation command and immutable helper/input hashes are recorded in `DERIVATION-PROVENANCE.json`; the script is `tools/derive_summary49.py`. `summary-derived.json` reproduces both means and bootstrap intervals in `summary.json` exactly.
