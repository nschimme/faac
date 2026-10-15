# Final clean tier17 stack footprint

Matched GCC 13.3.0 x86_64 Linux release/LTO builds ran under Apple Silicon emulation with identical `/work/source` and `/tmp/faac-build` paths. This is code-size and allocation evidence, not native timing.

| Variant | `.text` | `.rodata` | `.data` | ROM total | `.bss` |
|---|---:|---:|---:|---:|---:|
| Stack parent | 65280 | 9896 | 1184 | 76360 | 20552 |
| Clean tier17 stack | 65392 | 9880 | 1184 | 76456 | 20552 |
| Delta | +112 | -16 | +0 | +96 | +0 |

| Channels | Persistent SBR allocation delta (resampler excluded) |
|---:|---:|
| 1 | +3616 B |
| 2 | +6176 B |
| 4 | +11296 B |
| 8 | +21536 B |

The four-envelope array capacity adds the persistent SBR allocation shown above. The dry writer's local scratch buffer grows from 1024 to 2048 bytes to cover a conservative 2-channel × 4-envelope × 64-band × 20-bit bound plus fixed fields; that is +1024 bytes of call-frame scratch, not persistent encoder state. The 196-stream identity matrix and LC/HE decoder gates are linked in the tier17 production-final archive. No timing claim is made.

The source patch and GCC13 build outputs, including section map, build log, library hash, allocation model output, and staged source hashes, are adjacent to this file and summarized in `clean-tier17-footprint.json`.
