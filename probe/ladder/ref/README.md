# Apple reference streams

Made on macOS 27 (Darwin 27.0.0) with `afconvert`, from the 49 clips in
`faac-benchmark/data/external/audio` (48 kHz, 16-bit stereo, same file names as `apple/`).

| Dir | Command | Mean rate | Output rate |
|---|---|---:|---|
| `apple/` (128k LC, existing) | `afconvert -f m4af -d aac -b 128000 IN OUT` (recovered, see below) | ~124k | 48 kHz |
| `apple_lc64k/` | `afconvert -f m4af -d aac@48000 -b 64000 IN OUT` | ~72k | 48 kHz |
| `apple_lc96k/` | `afconvert -f m4af -d aac@48000 -b 96000 IN OUT` | ~106k | 48 kHz |
| `apple_he32k/` | `afconvert -f m4af -d aach@48000 -b 32000 IN OUT` | ~38.5k | 24 kHz core, SBR to 48 kHz |

- The 128k settings were not recorded. They were recovered by re-encoding
  `12-German-male-speech.441.16b48k` with `-b 128000` and default strategy: same size
  (126356 B), same packet count, decoded PCM identical to `apple/`; only 18 header bytes differ.
  CBR (`-s 0`), VBR-ish `-s 2/3` and `-q 127` give different sizes.
- `@48000` is required. Without it afconvert silently drops the output rate at 64k/96k (to 32 kHz)
  and HE (to 16 kHz), which no longer matches the 48 kHz FAAC input.
- afconvert's default strategy is an ABR-like mode; the realised rate overshoots the request
  at low rates (64k -> 72k, 96k -> 106k, HE 32k -> 38.5k). Compare bits-adjusted, never at the nominal rate.
- HE `afinfo` "valid frames" is counted at the core rate (half the source frame count), e.g. velvet
  240000 vs 480000. This is the same core-rate units convention as the FAAC HE container.
- Delay is 2112 (priming) on all four sets, as in the existing 128k refs. For HE, check whether
  the priming is core-rate samples (2112 at 24 kHz = 4224 output samples) before applying the +64 pad.
