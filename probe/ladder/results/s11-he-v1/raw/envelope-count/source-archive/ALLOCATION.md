# Probe allocation model

Measured on the Apple Silicon build with the same compiler/configuration for frozen `99ce2b09` and the probe. Counts exclude the unchanged resampler allocation and codec core. The probe allocates its two `SBRInfo` dry-write/snapshot copies in every mode, including mode 0. Fixed-tier/fixed-four modes reserve one FIFO candidate; adaptive mode reserves three.

| Structure | Frozen baseline | Probe | Change |
|---|---:|---:|---:|
| `sizeof(SbrEnvData)` | 512 B | 1,024 B | +512 B |
| `sizeof(SbrFrameData)` | 8 B | 16 B | +8 B |
| `sizeof(SBRChannel)` | 4,376 B | 4,376 B | 0 B |
| `sizeof(SBRInfo)` | 2,508 B | 3,552 B | +1,044 B |
| `sizeof(SBRContext)` | 80 B | 120 B | +40 B |
| `sizeof(SignalAnalysisChannel)` | 520 B | 9,224 B | +8,704 B |
| `sizeof(SignalAnalysis)` | 32 B | 48 B | +16 B |

The FIFO has four slots. Baseline slot bytes are `8 + 512*n` for `n` channels. Probe fixed-mode slot bytes are `12 + 16 + 1,024*n`; adaptive slot bytes are `12 + 3*(16 + 1,024*n)`. Signal analysis allocates its header plus `n` channels. `SBRInfo` allocates its header plus `n` unchanged `SBRChannel` objects; the probe adds two more allocations of that same probe-sized object for trial and snapshot state.

| Channels | Baseline SBR context/FIFO + analysis + info | Probe fixed-mode total | Fixed-mode increase | Probe adaptive total | Adaptive increase |
|---:|---:|---:|---:|---:|---:|
| 1 | 9,596 B | 37,384 B | +27,788 B | 45,704 B | +36,108 B |
| 2 | 16,540 B | 63,832 B | +47,292 B | 80,344 B | +63,804 B |
| 8 | 58,204 B | 222,520 B | +164,316 B | 288,184 B | +229,980 B |

These are temporary experiment allocations, not a shipping footprint estimate. The adaptive mode stores slot-resolved energy in `SignalAnalysis` and three quantized alternatives in each delayed FIFO entry; the two `SBRInfo` copies support dry-write costing and immutability checks. No production ROM, timing, or stack measurement was run.
