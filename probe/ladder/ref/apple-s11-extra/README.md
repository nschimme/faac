# Apple AAC extra references (44.1 kHz source and 48 kHz LC 256/320k)

Apple `afconvert` streams for all 49 clips per rung, generated on macOS 27 with the benchmark's command
(`afconvert -f m4af -d aac|aach -b BPS -c 2`) from `faac-benchmark/data/external/audio_44k1` (44.1 kHz rungs) and
`audio` (48 kHz rungs). The 4-clip subset matches the benchmark's earlier output bytes. Complements `../apple-s11-367cf8dc/` (48 kHz LC 96-192k, HE 40-64k).

- `lc/44k1_{128k,160k,192k,256k}`, `he/44k1_64k`: 44.1 kHz-source scenarios.
- `lc/48k_{256k,320k}`: 48 kHz scenarios above the S11 rungs.
- 49 clips per rung, named `<wav name>.m4a`.
- `manifest.json`: scenario, size, SHA-256 per stream.

Black-box reference media only; no Apple code or binaries. Command template as in the S11 README
(`afconvert -f m4af -d aac|aach -b BPS`); the exact historical command is unrecorded.
