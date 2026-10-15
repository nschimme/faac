# PR70 CI and footprint evidence

This directory records GitHub Actions evidence for PR #70 at head `c50b66aab79f754fc17e2550159a861d26403b65`.

The build run [38659018090](https://github.com/nschimme/faac/actions/runs/38659018090) completed successfully; all ten jobs reported `completed/success`. Raw run and jobs API JSON are in [`api/`](api/).

The benchmark run [38659018069](https://github.com/nschimme/faac/actions/runs/38659018069) is terminal with overall conclusion **failure**. Footprint, arm64 CBR, and amd64 ABR jobs succeeded; arm64 VBR failed its gapless gate. The final consolidated artifact says “Gapless Regression Detected.” The full terminal disposition, ABR warning cases, hashes, and limitations are in [`FINAL-CI-RESULTS.md`](FINAL-CI-RESULTS.md). Terminal API snapshots are in [`api/`](api/). The VBR and ABR job logs were retrieved through authenticated GitHub CLI access and scanned for exposed credentials; see [`log-access.txt`](log-access.txt).

The VBR artifact compares baseline and candidate: each contains 1,341 matrix rows. Three 48 kHz stereo 16 kb/s HE-AAC v1 streams have nonzero gapless lag in both baseline and candidate: −964, −391, and −89 samples. All three have `elst` and `itunsmpb` metadata and `frame_error: 0`; their lag values match exactly across baseline/candidate. The baseline report has the same three lag values, so these rows do not show a candidate-only change in reported lag. The artifact’s VBR gate still reports failure; the report-level match does not establish decoded PCM alignment, sample-exactness, or the underlying cause. See [`vbr-gapless-paired-cases.json`](vbr-gapless-paired-cases.json) for exact paired rows and artifact/report hashes. The quality agent owns detailed repro assets and further attribution.

The footprint annotation reports `.text+.rodata+.data 76040 -> 75992 (-48 bytes, -0.06%)`. The local GCC 13.3/LTO `size` measurement recorded in [`summary.json`](summary.json) is 76,072 to 76,056 bytes (-16 bytes). The measurements use different runner/toolchain/build settings and are kept separate.

Exact source versions are under [`workflows/`](workflows/), with hashes and refs in [`source-versions.json`](source-versions.json), and benchmark comparator sources under [`benchmark-source/`](benchmark-source/). [`file-manifest.json`](file-manifest.json) records byte counts and SHA-256 hashes for package files. No audio, encoded streams, build products, or credentials are included.
