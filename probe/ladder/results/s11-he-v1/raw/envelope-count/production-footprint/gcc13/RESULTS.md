# Matched GCC 13 production footprint: fixed four-envelope support

GCC 13.3.0 x86_64 Linux release/LTO builds were made in separate Docker runs with the same `/work/source` and `/tmp/faac-build` paths. The amd64 image ran under Apple Silicon emulation; these are code-size and `sizeof` measurements, not native performance results.

| Variant | `.text` | `.rodata` | `.data` | ROM total | `.bss` | Library SHA-256 |
|---|---:|---:|---:|---:|---:|---|
| base | 65136 | 9896 | 1184 | 76216 | 20552 | `a5750ca531aa6b089216d29a9ca6605b43baabfa3ee7a742a65319a28b41a104` |
| all4_base | 65184 | 9928 | 1184 | 76296 | 20552 | `a305698357db15861ef079bb2ea70fbfa36725cf69035ab8b66703af4a1efd5b` |
| stack | 65280 | 9896 | 1184 | 76360 | 20552 | `21700f768145ffd8ec0cec8ff1e370741e309d580dad28791ed9313a56ac450a` |
| all4_stack | 65328 | 9928 | 1184 | 76440 | 20552 | `bcb8dee5b36b364c7014c7ed33f7a8d7e6c5a01ac7a98cfbb82e7922b4d29583` |

| Four-envelope ROM delta | Bytes |
|---|---:|
| Baseline all4 minus upstream baseline | 80 |
| Stack all4 minus retained stack parent | 80 |

The production allocation model sums `sizeof(SBRInfo) + n * sizeof(SBRChannel)`, `sizeof(SBRContext) + SBR_FRAME_FIFO * (sizeof(SbrFrameData) + n * sizeof(SbrEnvData))`, and `sizeof(SignalAnalysis) + n * sizeof(SignalAnalysisChannel)`. It excludes resampler allocations and allocator overhead.

| Channels | Baseline total | Base all4 total | Base delta | Stack parent total | Stack all4 total | Stack delta |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 9596 | 13212 | +3616 | 9596 | 13212 | +3616 |
| 2 | 16540 | 22716 | +6176 | 16540 | 22716 | +6176 |
| 4 | 30428 | 41724 | +11296 | 30428 | 41724 | +11296 |
| 8 | 58204 | 79740 | +21536 | 58204 | 79740 | +21536 |

The GCC13 section output, build logs, library hashes, production-size model output, source-tree hashes, and patch hashes are recorded in `footprint.json` and the adjacent variant folders. All4 packet and strict FDK PCM equivalence counts are 196/196 for both base and stack contexts.
