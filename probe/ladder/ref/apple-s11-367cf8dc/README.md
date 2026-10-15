# Apple AAC S11 reference media

This directory is a dedicated reference-media exception to the S11 package's raw-metadata-only rule. It contains 392 existing Apple AAC `.m4a` reference streams, copied byte-for-byte from the output paths recorded in `faac-benchmark/comparison_results.json`. No streams were generated or re-encoded during this capture. Previous Apple reference paths under `probe/ladder/ref/apple/` remain untouched.

The manifest records each comparison row, original WAV path and SHA-256, output path, row-reported and captured byte sizes, current encoded-stream SHA-256, copied-stream SHA-256, header validation, scenario/profile/target/actual bitrate, duration, and decoder metadata. It freezes the input JSON SHA-256 (`18524a20abfa82d83ea766807f743e97949d6e4a69e50765ca78ddd0f1602451`) and header-validation JSON SHA-256 (`55da0163a82b229c9f037da19a3cf1b09d0d71b8045cd45bc937802160abcdf3`). The current capture checks confirm all 392 expected streams are readable, each on-disk byte size equals its row's recorded `size`, all copied hashes match source output hashes, and all matched header records report the expected 48 kHz stereo profile. This capture does not retroactively prove that these were the bytes used for an earlier scoring run; the historical matrix has no encoded-stream hashes.

## Captured rungs

- HE rows (`apple_aac_27_0_1_he`): 48 kHz stereo at 40, 48, 56, and 64 kb/s; 49 clips per rung. Header validation reports HE-AAC, 48,000 Hz, two channels.
- LC rows (`apple_aac_27_0_1_lc`): 48 kHz stereo at 96, 128, 160, and 192 kb/s; 49 clips per rung. Header validation reports LC, 48,000 Hz, two channels.

Files are arranged as `he/40k/<original-filename>.m4a` and `lc/96k/<original-filename>.m4a`. `manifest.json` is the complete row-level index.

## Provenance and reproduction boundaries

The matrix labels the tool `Apple AAC 27.0.1`. Its original rows contain no command argv or output-file SHA-256. The current `faac-benchmark` checkout at `bdce18f7123f51c31795afe9fb4571c18f507a77` has `codec_bench/encoders.py` SHA-256 `7da9fbce6262bd6501f2692966ae96cf39a7d582835fa6228bd9a03eabc4af17`. Its `AFConvertEncoder` builder has the command template `afconvert -f m4af -d {aac|aach} -b {bps} -c {channels} {input} {output}`. The source label may fall back to the macOS version when an `afconvert` version cannot be parsed, so do not treat the row label as an independently verified encoder binary version. The exact historical command and original encoded bytes remain unproven.

Do not copy Apple encoder code or binaries. This is black-box reference media only. The `comparison_results.json` rows call their decoder `FFmpeg AAC`, but they do not bind the historical scores to a decoder binary hash. A separate S11 decoder provenance set is recorded for reproducible Linux decode checks below; that does not retroactively establish the scorer used for this matrix.

## Linux decoding and source WAV checks

The original input WAV corpus is not included. Fetch the `faac-benchmark` external audio corpus into `$FAAC_BENCHMARK_DIR/data/external/audio/`; validate each file against the per-row `source_wav_sha256` in `manifest.json` before using it. The captured source paths were under `/Users/nschimme/gitprojects/faac-benchmark/data/external/audio/`. The manifest has 49 unique WAV sources, each repeated across eight rungs. For example, this verifies every distinct source WAV before a rerun:

```sh
python3 - "$FAAC_BENCHMARK_DIR" manifest.json <<'PY'
import hashlib, json, pathlib, sys
root = pathlib.Path(sys.argv[1]) / "data"
manifest = json.load(open(sys.argv[2]))
seen = {}
for row in manifest["rows"]:
    rel = pathlib.Path(row["source_wav_path_at_capture"]).as_posix().split("/data/", 1)[1]
    expected = row["source_wav_sha256"]
    if rel in seen and seen[rel] != expected:
        raise SystemExit(f"inconsistent expected hash: {rel}")
    seen[rel] = expected
for rel, expected in sorted(seen.items()):
    path = root / rel
    actual = hashlib.sha256(path.read_bytes()).hexdigest()
    if actual != expected:
        raise SystemExit(f"hash mismatch: {path}: {actual} != {expected}")
print(f"verified {len(seen)} source WAVs")
PY
```

The decoder hashes recorded in separate S11 validation are historical macOS/AArch64 executable hashes, not portable Linux checksums and not requirements for this package. They are retained only as historical provenance: FAAD `21f01362a0db865542466da224a53ea033fe61faf21c6b6ec5313388760ae111`, FDK `f33f38e1b6bcfd3326c7ccd5863ef1fdd7ea0468df3a5c4eb698d9d9a91b2f18`, and Homebrew FFmpeg `e9fa823d0d91cb290b537e30b2a67d448accc1be858ededc8a0b98a3ce5f385a`. Historical build provenance is linked in [`fixed8 full49 provenance`](../../results/s11-he-v1/raw/transient-border/upstream098-fixed8-gate-v2/full49/provenance.json) and the [`FDK binary manifest`](../../results/s11-he-v1/raw/transient-border/upstream098-fixed8-gate-v2/validation-build/fdkdec-manifest.json). Do not compare Linux binaries against those macOS hashes.

For Linux reproduction, build fresh Linux-native decoder binaries from these source pins, then freeze the resulting Linux executable hashes and build/version details in the run manifest:

- FAAD2: `FreewareAdvancedAudio/faad2` commit `287c00ccf20086db395869cfe8b09fc50a76e988` (`2.11.4+287c00c`). The `faac-benchmark` build script at commit `bdce18f7123f51c31795afe9fb4571c18f507a77`, `scripts/build_faad2.sh`, pins and verifies this source archive.
- FDK decoder: libfdk-aac `2.0.0`, plus the `fdkdec` wrapper source at the same `faac-benchmark` commit (`scripts/fdkdec/fdkdec.c`, SHA-256 `59aeba848fbe1ee306655e8fe0c0a111fb5de5c7a78917bc08ae3c30394eb233`). Ensure `pkg-config --modversion fdk-aac` reports `2.0.0` before invoking `scripts/build_fdkdec.sh`; use the matching FAAC/libfaam source/build described in the linked manifest.
- FFmpeg: upstream revision `N-127192-gef52e1cc38` (revision prefix `ef52e1cc38`); build that revision for Linux and record its full checkout commit and `ffmpeg -version` output.

These pins identify source versions; Linux binary hashes will differ by host, compiler, and build options. Capture them after building and use those exact same decoder executables for every reference/candidate arm in a comparison. Do not build a different decoder for each arm. The historical comparison rows themselves only record `decoder_name=FFmpeg AAC`; the above pins support a new controlled decode, not proof of which binary produced the old MOS results.

Example Linux checks and decode commands (set the binary and source paths to the fresh Linux builds; keep output WAVs outside this package):

```sh
export FAAD_BIN=/path/to/linux/faad
export FDKDEC_BIN=/path/to/linux/fdkdec
export FFMPEG_BIN=/path/to/linux/ffmpeg
export FAAD2_SRC=/path/to/faad2-287c00c-source

# Verify the source pins and FDK version, then freeze Linux executable hashes before either arm.
git -C "$FAAD2_SRC" rev-parse HEAD
pkg-config --modversion fdk-aac
"$FFMPEG_BIN" -version | head -n 1
sha256sum "$FAAD_BIN" "$FDKDEC_BIN" "$FFMPEG_BIN" > linux-decoder-binaries.sha256

"$FAAD_BIN" --strict --no-gapless -q -o /tmp/apple-reference-faad.wav he/40k/12-German-male-speech.441.16b48k.wav.m4a
"$FDKDEC_BIN" he/40k/12-German-male-speech.441.16b48k.wav.m4a /tmp/apple-reference-fdk.wav
"$FFMPEG_BIN" -v error -xerror -i he/40k/12-German-male-speech.441.16b48k.wav.m4a -map 0:a:0 -f null -
```

Use the same executable paths and frozen hash file when decoding both arms. A successful strict decode establishes decode acceptance, not MOS reproduction or historical decoder identity. Use macOS only if new Apple AAC streams need to be generated; this package preserves existing streams and contains no Apple encoding software.
