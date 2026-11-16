# Apple AAC 48 kHz references (current faac-benchmark corpus)

Replaces every earlier 48 kHz Apple set (`apple/`, `apple_*`, `apple-s11-367cf8dc/`, `apple-s11-extra/lc/48k_*`), which were made from the old
49-clip corpus trimmed to at most 10 s. Those remain in git history. `apple-s11-extra/{lc,he}/44k1_*` is untouched (44.1 kHz, not rebuilt).

- Source: faac-benchmark 9f5f102, `data/external/audio` rebuilt with `setup_datasets.setup_audio` (full-length clips, 77, 5-30 s, 48 kHz stereo; ffmpeg 9.0.2).
- Encoder: `/usr/bin/afconvert` on macOS 27.2 (26B123), argv from `AFConvertEncoder.get_encode_cmd`: `afconvert -f m4af -d {aac|aach|aacp} -s 0 -b BPS -c 2 IN OUT` (-s 0 = CBR).
  Exactly what the leaderboard scored, so these streams are what FAAC is compared against. Per-stream argv, sizes, durations, sha256 are in `manifest.json`.
- Sibling sets: `../fdk-48k/` and `../xaac-48k/` (same corpus and rungs; libxaac is HE/HE-v2 only).
- Layout: `{lc,he,hev2}/<kbps>k/<clip>.wav.m4a`. 924 streams, all strict-decode clean (`ffmpeg -xerror`).

| Dir | Profile | Rungs | Why |
|---|---|---|---|
| `lc/` | AAC-LC | 128, 160, 192, 256, 320 | Apple ties or wins the mean MOS at 192k and 320k; neighbours give the BD-rate curve |
| `he/` | HE-AAC v1 | 32, 40, 48, 64 | Apple wins stereo fidelity and P1 at the low rungs |
| `hev2/` | HE-AAC v2 | 24, 32, 48 | Apple wins HE-v2 P1 at 32k_stereo_48k, ties 24k |

Caveats:
- No `@48000` is passed (the benchmark does not either). HE 32k streams therefore declare a 32 kHz output rate; the others are 48 kHz. Keep scoring the same way the benchmark does.
- Realized rates run 2-3 % above the target (LC 128k: 131.4 kbps mean; HE 64k: 66.5), so compare at realized bitrate, not nominal.
- The WAVs are not committed. A Linux rebuild resamples with a different ffmpeg; check `src_sha256` in the manifest and expect mismatches on resampled SoundExpert clips.
- Not here: the gist's Apple pre-echo (NPER) lead at 16 kHz mono speech and 44.1 kHz LC; those need their own rebuilds.
