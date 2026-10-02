# <img src="frontend/faac.svg" alt="FAAC" width="48" height="48" align="top" /> Freeware Advanced Audio Coder

This repository is a monorepo for the FAAC AAC encoder and its companion FAAD3 decoder, built together with Meson.

FAAC is an open-source, dependency-free AAC encoder aimed at embedded and pipeline use cases where footprint and throughput matter as much as quality. FAAD3 is the in-tree AAC decoder, used for decoding, round-trip testing and quality benchmarking.

### Key features:

- **`libfaac` (Encoder Library):**
  - MPEG-2 & MPEG-4 AAC-LC and HE-AAC v1 (SBR) encoding
  - VBR, ABR, and CBR rate control
  - Advanced encoding tools: Dynamic block-switching, PNS, and TNS
  - Output bitstreams: ADTS bitstream and raw AAC frames
  - Sample rates from 8 kHz to 96 kHz, supporting mono up to 7.1 multichannel
- **`libfaad` (Decoder Library):**
  - MPEG-2 & MPEG-4 AAC-LC, HE-AAC v1 (SBR), and HE-AAC v2 (Parametric Stereo) decoding
  - Decodes ADTS streams and raw AAC access units (with AudioSpecificConfig)
  - Output formats: 16-bit integer and 32-bit floating-point PCM, with optional downmixing
- **Frontends (`faac` & `faad` CLI tools):**
  - Full MP4/M4A container read and write support via built-in MP4 demuxer and muxer
  - Flexible file input/output and stdout/stdin piping

## Repository layout

| Directory | Contents |
|-----------|----------|
| `libfaac/` | Encoder library. Public API is `faac_*` in `include/faac.h` |
| `libfaad/` | FAAD3 decoder library (LC, SBR, PS). Public API in `include/faad.h` |
| `common/` | Source shared by both libraries and the frontends: FFT engine, SBR/Huffman/SFB tables, endian and AudioSpecificConfig helpers. Not built as a library of its own |
| `frontend/` | `faac` and `faad` command-line tools (plus `faacgui` on Windows), sharing the input/output code, the audio-only MP4 writer and the MP4 reader |
| `include/` | Public headers |
| `tests/` | Decoder smoke/robustness tests, frontend round-trip test, libFuzzer harnesses, quality benchmark wrapper |
| `docs/` | Man page and API documentation |

### Command-line tools:

- `faac` writes MP4/M4A with gapless playback info, as well as raw ADTS
- WAV and raw PCM input, with stdin/stdout piping

## Copyrights

FAAC is free software, licensed under the GNU Lesser General Public License (LGPL), version 2.1 or later:

```
FAAC - Freeware Advanced Audio Coder
Copyright (C) 1999-2001, Menno Bakker
Copyright (C) 2002-2017, Krzysztof Nikiel
Copyright (C) 2004, Dan Villiom P. Christiansen
Copyright (C) 2005-2026, Fabian Greffrath
Copyright (C) 2026, Nils Schimmelmann

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.
```

> **Important:** The use of this software may require the payment of patent royalties. You need to consider this issue before you start building derivative works. We are not warranting or indemnifying you in any way for patent royalties! **YOU ARE SOLELY RESPONSIBLE FOR YOUR OWN ACTIONS!**

## Compiling Instructions

1. Make sure you have recent versions of meson and ninja installed.
2. cd to FAAC source dir
3. Run:
   ```bash
   meson setup build
   ninja -C build
   meson test -C build
   meson install -C build
   ```

This builds `libfaac`, `libfaad` and the `faac` and `faad` frontends.

### Build options

| Option | Default | Description |
|--------|---------|-------------|
| `frontend` | true | Build the `faac` (and `faad`, if enabled) command-line tools |
| `decoder` | true | Build `libfaad` and the `faad` frontend |
| `decoder-sbr` | true | SBR (HE-AAC v1) support in `libfaad` |
| `decoder-ps` | true | Parametric Stereo (HE-AAC v2) support in `libfaad` |
| `max-channels` | 8 | Maximum number of channels (1-8) |
| `sbr-decimation` | 1 | Encoder SBR analysis density (1 = full quality, up to 8 = faster) |
| `stats` | false | End-of-encode diagnostics on stderr (instrumentation only) |

For an encoder-only build, pass `-Ddecoder=false`.

Library integration: [FAAC encoder API](docs/libfaac.md).

## Usage

```bash
faac input.wav -o output.m4a        # encode
faad output.m4a -o decoded.wav      # decode
faad -i output.m4a                  # show stream info
```

See `faac --help`, `faad --help` and `docs/faac.1` for all options.
