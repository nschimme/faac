# Matched GCC13 production footprint: tier17 envelope threshold

GCC 13.3.0 x86_64 Linux release/LTO builds used the same `/work/source` and `/tmp/faac-build` paths in separate Docker runs. The amd64 image ran under Apple Silicon emulation; these measurements cover code size and allocation structure, not native performance.

| Variant | `.text` | `.rodata` | `.data` | ROM total | `.bss` | Library SHA-256 |
|---|---:|---:|---:|---:|---:|---|
| base | 65136 | 9896 | 1184 | 76216 | 20552 | `a5750ca531aa6b089216d29a9ca6605b43baabfa3ee7a742a65319a28b41a104` |
| tier17_base | 65328 | 9904 | 1184 | 76416 | 20552 | `8180e1408be49fe3f68a218f0400db4ec60c8f961511bc73320d4d914aeef869` |
| stack | 65280 | 9896 | 1184 | 76360 | 20552 | `21700f768145ffd8ec0cec8ff1e370741e309d580dad28791ed9313a56ac450a` |
| tier17_stack | 65472 | 9904 | 1184 | 76560 | 20552 | `54c80e44097991f217e9bb0d029056f5c97af412bedfa72f98af76320e5e0be3` |

| Context | ROM delta | Allocation delta at 1 / 2 / 4 / 8 channels |
|---|---:|---|
| upstream base → tier17 | 200 B | 3616 / 6176 / 11296 / 21536 B |
| retained stack parent → tier17 | 200 B | 3616 / 6176 / 11296 / 21536 B |

Allocation totals use `sizeof(SBRInfo) + n*sizeof(SBRChannel)`, `sizeof(SBRContext) + FIFO*(sizeof(SbrFrameData) + n*sizeof(SbrEnvData))`, and `sizeof(SignalAnalysis) + n*sizeof(SignalAnalysisChannel)`. Resampler storage and allocator overhead are excluded. Exact 196/196 packet and strict FDK PCM matches are recorded for baseline and retained-stack contexts in the production-equivalence archive.

The four build logs, section listings, library hashes, size-model output, source tree hashes, and tier17 source patches are adjacent to this report and summarized in `footprint.json`.
