# <img src="frontend/faac.svg" alt="FAAC" width="48" height="48" align="top" /> Freeware Advanced Audio Coder

FAAC is an open-source, dependency-free AAC encoder, and FAAD its decoder counterpart. Both are aimed at embedded and pipeline use cases where footprint and throughput matter as much as quality.

### Key features:

- MPEG-4 AAC-LC and HE-AAC v1 (SBR) encoding and decoding, plus HE-AAC v2 (PS) decoding
- Sample rates from 8 kHz to 96 kHz, supporting mono up to 7.1 multichannel
- VBR, ABR and CBR rate control
- Advanced encoding tools: Dynamic block-switching, PNS, and TNS
- Flexible output options: ADTS and raw AAC streams

### Command-line tools:

- `faac` and `faad` write and read MP4/M4A with gapless playback info, as well as raw ADTS
- WAV/RF64 and raw PCM input, with stdin/stdout piping
- FAAD automatically writes RF64 for large WAV output and reads compressed inputs beyond 4 GiB
- `faam` muxes ADTS AAC and Annex-B H.264/H.265 into MP4/M4A/M4B, and inspects, demuxes, tags and chapters existing files. H.264/H.265 needs `-Dmuxer-video=true` and reading fragmented MP4 needs `-Dmuxer-fragmented=true` (both on by default)

## Copyrights

FAAC, FAAD and FAAM are free software, licensed under the GNU Lesser General Public License (LGPL), version 2.1 or later:

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
2. cd to the source dir
3. Run:
   ```bash
   meson setup build
   ninja -C build
   meson install -C build
   ```

### Build options

| Option | Default | Description |
|--------|---------|-------------|
| `encoder` | true | Build `libfaac` and the `faac` frontend |
| `decoder` | true | Build `libfaad` and the `faad` frontend |
| `frontend` | true | Build the command-line tools for the enabled components |
| `frontend-static` | false | Link the frontend executables to the static libraries (libfaac, libfaad, libfaam) |
| `decoder-sbr` | true | SBR (HE-AAC v1) support in `libfaad` |
| `decoder-ps` | true | Parametric Stereo (HE-AAC v2) support in `libfaad` |
| `max-channels` | 8 | Maximum number of channels (1-8) |
| `muxer` | true | Build the `faam` command-line tool (`libfaam` itself is always built) |
| `muxer-video` | true | `libfaam` and `faam`: H.264/H.265 video tracks; off leaves an audio-only muxer/demuxer |
| `muxer-fragmented` | true | `libfaam`: fragmented MP4 (moof/mdat) crash-safe recording and demuxing; `faam` reads such files but does not write them |
| `sbr-decimation` | 1 | Encoder SBR analysis density (1 = full quality, up to 8 = faster) |
| `stats` | false | End-of-stream diagnostics on stderr, encoder and decoder (instrumentation only) |

For an encoder-only build pass `-Ddecoder=false`; for a decoder-only build pass `-Dencoder=false`.
Decoder PS support requires SBR and at least two compiled channels;
`faad_get_library_info()` reports the capabilities of the loaded build.

Library integration: [FAAC encoder API](docs/libfaac.md), [FAAD decoder API](docs/libfaad.md) and
[FAAM MP4 muxer/demuxer API](docs/libfaam.md).

## Usage

```bash
faac input.wav -o output.m4a        # encode
faad output.m4a -o decoded.wav      # decode
faad -i output.m4a                  # show stream info
faam -i output.aac -o output.m4a    # mux ADTS into MP4
faam tag output.m4a --title Song    # edit tags in place
```
