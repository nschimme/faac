# Upstream 098 fixed8 build and decoder validation

The build manifest records two isolated Meson release builds at upstream
`098e8900289b87d1592a859351ca0bd420397d4b`: a clean baseline and a fixed8
candidate with the single-file `libfaac/sbr_analysis.c` transient-border
change. The candidate changes the VARFIX inner border to slot 8 while keeping
the existing transient detector and two-envelope layout. It adds no fields,
allocations, or initialization state.

Both encoder builds completed with Apple clang 21 on arm64, Meson release,
optimization level 3, LTO enabled, `max-channels=8`, `sbr-decimation=1`, and
`stats=false`. The manifests include executable, library, source-diff, clip,
and input source hashes. Native timing, GCC-13/LTO footprint, and short-clip interop results are archived below.

`fdkdec-manifest.json` records a separate FDK binary built from benchmark HEAD
`bdce18f7123f51c31795afe9fb4571c18f507a77`, linked with the isolated upstream
098 static libfaam. It was written to `/private/tmp`; the shared
`faac-benchmark/bin/fdkdec` was left untouched. The old scorer binary hash is
`32528021e276e28fd54874dfda806c7eb4a73e67796b4a39c811991dc0f7cbbf`; the
new isolated binary hash is
`f33f38e1b6bcfd3326c7ccd5863ef1fdd7ea0468df3a5c4eb698d9d9a91b2f18`.

The full-matrix comparison covers the 392 frozen rows in `full49/rows.json`
(196 baseline and 196 fixed8). The new FDK binary was run on each encoded
stream; old and new decoded WAVs were normalized through the benchmark's
`wav_conv(rate=48000, channels=2)` and compared by sample PCM hash and shape.
All 392 rows matched. The row source hash is recorded in both manifests.

The 16-row HE fixed8 comparison also matches old and new sample PCM and
48 kHz stereo frame shape for all rows. Whole-WAV hashes differ from archived
rows only because their RIFF headers differ; PCM bytes, frame counts, channel
counts, and rates match exactly.

The LC tail matrix uses generated 16-bit PCM sine/burst inputs at 16 kHz, six
lengths (135610, 136090, 136091, 136092, 136570, and 136571 frames), mono and
stereo, forced LC at 20 and 48 kbps. All 24 old/new decodes match in PCM,
whole-WAV hash, gapless metadata, and output shape. In particular, the
136091-frame target decodes to exactly 136091 frames at 16 kHz; its MP4
end-padding is 101 samples, below 481. Twelve rows across 136090–136092 frames
have padding below 481 and all preserve the exact input frame count.

Evidence JSON files retain their exact `/private/tmp` artifact paths:

- `full392-normalized-equivalence.json`
- `he16-decoder-equivalence.json`
- `lc-tail-ab.json`

Run the 392-row decoder comparison with:

```sh
../faac-benchmark/.venv/bin/python \
  .cache/he-v1-feature-study/transient-border/upstream098-fixed8-gate-v2/validation-build/validate_fdk_equivalence.py
```


Latest-098 production checks were run on isolated baseline and fixed8 builds at `sbr-decimation=4`. `production-smoke-results.json` records four synthetic input classes (tone/silence × mono/stereo), 2,049 frames at 48 kHz, forced-LC CBR 48 ADTS exact-byte equality, and HE-AAC v1 CBR 48 MP4 encode/decode success through the isolated FDK decoder. The decoder returned 3,134 stereo 48 kHz frames for each short HE input. This is a smoke/interoperability check, not an exact gapless-duration test.

`meson test -C build-release --print-errorlogs` on the fixed8 build reported `No tests defined.` No RSS measurement was collected.

`gcc13-lto-footprint.json` records the matched GCC 13.3/LTO section-size comparison: text -64 B, rodata +48 B, data 0 B, BSS 0 B, GNU size total -16 B. The Linux amd64 build ran under arm64 emulation, so these data are footprint only. `native-timing-results.json` and `native-timing-summary.json` preserve the raw paired runs and additive mean/median summaries; the mean pooled delta is +0.0296% (bootstrap 95% CI -0.3924% to +0.4123%), with no outlier exclusions.


The published source commit candidate is recorded in `source-commit-manifest.json`: `c50b66aa` on branch `upstream098-fixed8`, with parent `098e8900289b87d1592a859351ca0bd420397d4b`. It changes only `libfaac/sbr_analysis.c`; the commit patch changes the transient VARFIX border to slot 8. The worktree is clean.

`three-decoder-smoke.json` records ffprobe plus FAAD2 2.11.4, FDK, and FFmpeg decoding of all eight decimation-4 HE-AAC v1 CBR streams (tone/silence × mono/stereo × baseline/fixed8). All identify as HE-AAC and decode successfully. FAAD2 and FFmpeg return 2,050 frames and preserve input channel count for each 2,049-frame source. FDK returns 3,134 stereo frames for every case, including mono input; baseline and fixed8 shapes match. These short-stream results establish decoder acceptance, not gapless sample-count equivalence.

`timing-stream-validation.json` covers four regenerated 120-second timing representatives. Each stream exactly matches the corresponding pair-0 timing-run SHA, reports HE-AAC at 48 kHz stereo and 120 seconds / 2,815 packets, and decodes through FDK. FDK returns 5,761,086 frames on all four (input length is 5,760,000); baseline and fixed8 match by shape within each clip. This validates profile and stream duration, not exact decoded gapless length.

The GCC 13.3 linked-library sizes are retained as raw output and binaries. GNU `size` text+data is 76,072 B baseline and 76,056 B fixed8; `.text` is 65,056→64,992 B, `.rodata` 9,832→9,880 B, `.data` 1,184 B unchanged, and `.bss` 20,552 B unchanged. The `size -A` total is 102,934→102,918 B. The platform was linux/amd64 under arm64 emulation, so this is a footprint comparison only. No RSS measurement was collected.

A prior generated metadata typo wrote the source hash with final digit `d` in the first footprint/smoke draft. Those files were corrected before archive; `source-commit-manifest.json` and both clone `git rev-parse HEAD` checks identify the actual source base as `098e8900289b87d1592a859351ca0bd420397d4b`. No source changes occurred during that correction.


Patch provenance follow-up: the original `/private/tmp/upstream_fixed8.patch` remains untouched. Although its SHA matches the earlier manifest, it lacks a final newline and `git apply` rejects it. `source-committed.patch` is the canonical parseable `git diff` from the recorded parent to commit; its hash and the successful `git apply --check` on a clean disposable baseline clone are in `source-patch-provenance.json`. Both original baseline/fixed8 source clones remained clean.
