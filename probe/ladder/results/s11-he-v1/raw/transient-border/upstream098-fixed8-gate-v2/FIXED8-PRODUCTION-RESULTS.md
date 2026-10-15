# Fixed8 production cost and smoke results on merged upstream 098

Candidate: `sbr: fix transient inner envelope at slot 8`, source commit `c50b66aab79f754fc17e2550159a861d26403b65`, parent `098e8900289b87d1592a859351ca0bd420397d4b`; only `libfaac/sbr_analysis.c` changed (one insertion, five deletions). The source manifest and all run evidence are in [validation-build](validation-build/README.md).

- GCC 13/LTO amd64-emulated library: GNU size text+data decreased from 76,072 B to 76,056 B (−16 B); text −64 B, rodata +48 B, data unchanged, BSS 20,552 B unchanged. This is a footprint measurement, not native timing.
- Native Apple Clang/AArch64: 40 paired timings; mean duration change +0.029598%, paired-bootstrap 95% CI [−0.392430%, +0.412281%], median +0.251828%. No resolved speed change.
- Production smoke: 12 encoder rows at sbr-decimation=4. Four AAC-LC ADTS cases (tone/silence × mono/stereo) had exact baseline/fixed8 bitstream identity. Eight HE-AAC v1 tone/silence cases decoded successfully with FDK, FAAD2, and FFmpeg. HE baseline/fixed8 decoded frame counts matched (3,134), but their PCM was not asserted identical.
- Separate 16 kHz LC tail check: 24 mono/stereo × 20/48 kb/s cases decoded to expected exact source length; old/new FDK PCM matched in all 24. This does not generalize gapless behavior to every format or decoder configuration.
- No Meson tests were defined in this checkout, so no Meson test suite result is claimed.

Evidence: [footprint](validation-build/gcc13-lto-footprint.json), [native timings](validation-build/native-timing-summary.json), [smoke](validation-build/production-smoke-results.json), [three-decoder smoke](validation-build/three-decoder-smoke.json), [LC tail comparison](validation-build/lc-tail-ab.json), [full49 FDK equivalence](validation-build/full392-normalized-equivalence.json), [source manifest](validation-build/source-commit-manifest.json).

The candidate is published as [PR #70](https://github.com/nschimme/faac/pull/70), a new branch from upstream/master. Its build and benchmark workflow jobs are queued; their results remain the final outstanding gate.
