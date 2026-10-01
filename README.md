# <img src="frontend/faac.svg" alt="FAAC" width="48" height="48" align="top" /> Freeware Advanced Audio Coder

FAAC is an open-source, dependency-free AAC encoder aimed at embedded and pipeline use cases where footprint and throughput matter as much as quality.

### Key features:

- MPEG-4 AAC-LC and HE-AAC v1 (SBR) profiles
- Sample rates from 8 kHz to 96 kHz, supporting mono up to 7.1 multichannel
- VBR, ABR and CBR rate control
- Advanced encoding tools: Dynamic block-switching, PNS, and TNS
- Flexible output options: ADTS and raw AAC streams

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
2. cd to the source dir
3. Run:
   ```bash
   meson setup build
   ninja -C build
   meson install -C build
   ```

Build options

| Option | Default | Description |
|--------|---------|-------------|
| `frontend` | true | Build the `faac` command-line tool |
| `max-channels` | 8 | Maximum number of channels (1-8) |
| `sbr-decimation` | 1 | Encoder SBR analysis density (1 = full quality, up to 8 = faster) |
| `stats` | false | End-of-stream diagnostics on stderr (instrumentation only) |

Library integration: [FAAC encoder API](docs/libfaac.md).

## Usage

```bash
faac input.wav -o output.m4a        # encode
```
