# Final tier17 stack production validation

The final isolated production candidate is based on `5d1bd74703657ae6e9acc5fae1df63ce07190071`. It sets `numEnvFixFix` to 1 below 17 kb/s per channel and 4 at or above 17 kb/s per channel, preserving the existing transient two-envelope grid. The final source patch is archived at [clean-tier17-stack.patch](../../production-footprint/gcc13/tier17/clean-tier17-stack.patch); its SHA-256 is `d109b48eedec1990d3dd76148be1fa8b224192ccc8fac869c17bd39d35d86f34`.

The final native frontend SHA-256 is `7741458a69d12c7ceabd7e0b2b3e60f85f74620f29a28c7be49fb2f1c6b32d5d`; its shared libfaac SHA-256 is `32609f1838ef268fcbcb2c9f4fa9e72b7ee5ad5a98e5416ca66dadf1cead0ca6`. The source diff passes `git diff --check`.

## Quality-stream identity

`validate_clean_tier17.py --jobs 4 --output-dir .cache/he-v1-feature-study/envelope-count/tier17/production-final/identity-scratch2048` completed with exit 0. Across the full 49-clip, 32/48/64/96 kb/s matrix, all 196 ordered AAC packet identities and strict FDK-decoded PCM hashes match the measured `all_stack` streams above 32 kb/s; at 32 kb/s, all 49 match `stack_reference`. The machine-readable result is [equivalence-summary.json](identity-scratch2048/equivalence-summary.json).

`clean_tier17_production_gates.py --jobs 4 --output-dir .cache/he-v1-feature-study/envelope-count/tier17/production-final/gates-scratch2048` completed with exit 0. The LC matrix has 16/16 payload and PCM matches against the retained stack encoder. The HE boundary set has 16/16 FDK, FAAD `--strict`, and FFmpeg passes with HE-AAC profile asserted. It covers mono CBR 16/17/18 kb/s, stereo CBR 32/34/36 kb/s, and partial-flush mono 17/32 and stereo 34/64 kb/s. See [summary.json](gates-scratch2048/summary.json).

The final `sbr-decimation=4` release build completed with exit 0. `clean_tier17_decimation4_smoke.py` completed with exit 0 for forced HE-AAC v1 CBR mono silence at 17 kb/s, stereo tone at 64 kb/s, and partial stereo at 34 kb/s. All three streams passed FDK, FAAD `--strict`, and FFmpeg; see [summary.json](decimation4-smoke/summary.json).

`meson test -C build-tier17 --print-errorlogs` returned exit 0 with `No tests defined` for this frozen upstream source base. The native source builds and explicit decoder/CBR gates above are the available validation evidence.

## GCC13 footprint

The matched GCC13/LTO footprint, allocation model, build logs, library hash, and source-tree provenance are in [CLEAN-FOOTPRINT.md](../../production-footprint/gcc13/tier17/CLEAN-FOOTPRINT.md). Against the retained stack parent, the final candidate adds 96 bytes ROM and no `.bss`; the persistent SBR allocation grows by 6,176 bytes for stereo, excluding resampler and allocator overhead. The writer's stack scratch capacity increases by 1,024 bytes (from 1,024 to 2,048) to cover the conservative two-channel/four-envelope/64-band estimate. The amd64 image ran under Apple Silicon emulation, so no timing result is claimed.
