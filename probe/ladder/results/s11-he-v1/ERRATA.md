# S11 archive errata

The `raw/` subtree is byte-identical to the source cache and is never edited in place. The following corrected facts supersede any conflicting statement in copied reports:

1. The merged upstream baseline commit is `098e8900289b87d1592a859351ca0bd420397d4b` (last character `b`). Do not use the mistyped `...d4d` where that appeared in older generated narrative. The current validation cache's machine-readable JSON records the correct commit.
2. The adaptive selector's approximately 8 KiB/channel is persistent `slotBandE` storage, not scratch. It also performs a second QMF pass and exact dry bit scoring. No performance result is inferred from this structure.
3. In the HE smoke rows, FDK output had 3,134 frames; FAAD2 and FFmpeg each produced 2,050. Baseline and fixed8 frame counts matched within each decoder, but PCM was not asserted identical. Decoder-specific lengths do not establish universal gapless parity.
4. Fixed8's 392-row old/new FDK normalized-PCM identity is verified by `raw/transient-border/upstream098-fixed8-gate-v2/validation-build/full392-normalized-equivalence.json`. Adaptive mode1 full49 was decoded with the new FDK and has no 392-row old/new equivalence claim; the adaptive old/new comparison is limited to the 32-row gate.
5. PR #70 build workflow run `38659018090` succeeded. Benchmark run `38659018069` was active at the last status snapshot; it remains the only pending gate in the ledger snapshot.

See the current corrected [production results](PRODUCTION-RESULTS.md) and [Stage S11](../../LADDER_RESULT.md#stage-s11--bounded-he-v1-feature-study-and-merged-baseline-closure-2026-10-14). Historical raw files and their hashes remain unchanged.
