# Fixed8 production cost and smoke results on merged upstream 098

Candidate: `sbr: fix transient inner envelope at slot 8`, source commit `c50b66aab79f754fc17e2550159a861d26403b65`, parent `098e8900289b87d1592a859351ca0bd420397d4b`; only `libfaac/sbr_analysis.c` changed (one insertion, five deletions). The exact patch is `raw/transient-border/upstream098-fixed8-gate-v2/validation-build/source-committed.patch` (SHA-256 `1bb0d64ab8c13367f64044791301ba218f482c8f58f391f5c4fb925e045fc846`). Full manifests and raw evidence are in the relative `raw/transient-border/upstream098-fixed8-gate-v2/validation-build/` directory.

- GCC 13/LTO amd64-emulated on arm64: GNU size text+data decreased from 76,072 B to 76,056 B (−16 B); text −64 B, rodata +48 B, data unchanged, BSS 20,552 B unchanged. This is footprint, not timing.
- Native Apple Clang/AArch64: 40 paired timings; mean duration +0.029598%, paired-bootstrap 95% CI [−0.392430%, +0.412281%], median +0.251828%. No resolved speed change.
- Decimation-4 smoke: 12 encoder rows. Four LC ADTS cases (tone/silence × mono/stereo) had exact baseline/fixed8 bitstream identity. Eight HE-AAC v1 tone/silence cases decoded with FDK, FAAD2, and FFmpeg. For HE rows FDK returned 3,134 frames; FAAD2 and FFmpeg each returned 2,050. Baseline/fixed8 frame counts matched within each decoder, but PCM identity was not asserted. These outputs do not establish universal gapless parity.
- Separate 16 kHz LC-tail diagnostic: 24 mono/stereo × 20/48 kb/s cases returned expected source lengths; old/new FDK PCM matched on all 24. This does not generalize gapless behavior to every format or decoder configuration.
- No Meson tests were defined in the production-validation checkout, so no Meson test-suite pass is claimed.
- Adaptive selector structural cost (not measured performance): second QMF pass, exact dry bit scoring, and approximately 8 KiB/channel of persistent `slotBandE` storage.

Evidence files: `gcc13-lto-footprint.json`, `native-timing-summary.json`, `production-smoke-results.json`, `three-decoder-smoke.json`, `lc-tail-ab.json`, `full392-normalized-equivalence.json`, and `source-commit-manifest.json` in the linked validation-build directory.

The implementation was published as independent [PR #70](https://github.com/nschimme/faac/pull/70), based on upstream/master with the fixed8 patch only. Build workflow run `38659018090` succeeded; benchmark workflow run `38659018069` remained active at the last status snapshot. The benchmark result is the only outstanding validation gate.
