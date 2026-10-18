# Apple AAC extra references (44.1 kHz source and 48 kHz LC 256/320k)

Existing Apple AAC 27.0.1 streams copied byte-for-byte from `faac-benchmark/output/` (rows in
`comparison_results.json`); nothing re-encoded. Complements `../apple-s11-367cf8dc/` (48 kHz LC 96-192k, HE 40-64k).

- `lc/44k1_{128k,160k,192k,256k}`, `he/44k1_64k`: 44.1 kHz-source scenarios.
- `lc/48k_{256k,320k}`: 48 kHz scenarios above the S11 rungs.
- 4 clips per rung: 21-classic, fms, sandman, velvet (same names as `faac-benchmark/data/external/audio`).
- `manifest.json`: scenario, target/actual kb/s, size, SHA-256 per stream.

Black-box reference media only; no Apple code or binaries. Command template as in the S11 README
(`afconvert -f m4af -d aac|aach -b BPS`); the exact historical command is unrecorded.
