# Unconditional steady four-envelope production controls

These controls set `SbrAnalyze`'s steady FIXFIX envelope count to four while leaving the transient VARFIX count at two. The original stream-level `numEnvFixFix` thresholds and all header gating remain unchanged. Support changes allow four envelope arrays and encode the FIXFIX count in its existing two-bit field; VARFIX keeps a two-bit pointer.

Both builds use Meson/Ninja release, frontend enabled, dynamic library, max-channels 8, SBR decimation 1, stats disabled. Builds completed successfully.

| Control | Worktree and source base | Frontend SHA-256 | libfaac SHA-256 |
|---|---|---|---|
| base-all4 | `/private/tmp/faac-envcount-all4-base`, `99ce2b0980180194f9ccd1993a786208d277cf3e` | `c0dc1e7420c70657307af24d866786c3d15e37fee919834d43cd37157d7529b9` | `d2fb2eabd0dd13e79e726ffbc0715cc9e3625c10a716d3e437eb226e4418194a` |
| stack-all4 | `/private/tmp/faac-envcount-all4-stack`, `5d1bd74703657ae6e9acc5fae1df63ce07190071` | `7741458a69d12c7ceabd7e0b2b3e60f85f74620f29a28c7be49fb2f1c6b32d5d` | `9c488164a8e0e9321e8fbd5a8eda8290ec7393b180110cdc3cfdcb7b1ff74e00` |

Source patches are stored beside this file. Production equivalence is complete: all 196 streams in `all4_base` match `all_base`, and all 196 streams in `all4_stack` match `all_stack`, for ordered AAC packet identity and strict FDK-decoded PCM SHA-256. The corresponding machine-readable evidence is in `../production-equivalence/all4_base/equivalence-summary.json` and `../production-equivalence/all4_stack/equivalence-summary.json`.

Matched GCC 13.3.0 release/LTO measurements show +80 B ROM (`.text + .rodata + .data`) for four-envelope support versus each parent, with `.bss` unchanged at 20,552 B. The production allocation model excludes resampler memory and gives +3,616 B mono, +6,176 B stereo, and +21,536 B at eight channels. Details and hashes are in [the production footprint report](../production-footprint/gcc13/RESULTS.md).
