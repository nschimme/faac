# HE 48 kHz stereo scoreboard refresh on frozen baseline 098e8900 (Linux cloud)

Step 1 of the S11 plan: FAAC `098e8900289b87d1592a859351ca0bd420397d4b`
(`--object-type he-aac-v1 -b N`, ABR, MP4) versus the packaged Apple HE streams
(`ref/apple-s11-367cf8dc`), 40/48/56/64 kbps, 49 clips x 4 rungs, 0 invalid rows.
Decoder: FAAD2 `287c00c` (default gapless) for both arms. Scorer: faac-benchmark
`bdce18f7` zimtohrli path (`score_wav_pair`, bounded alignment). Bitrate: summed
AAC packet bytes / source duration. BD-rate: faac-benchmark PCHIP per clip,
clip-bootstrap CI (10000 resamples, seed 0). Positive = FAAC needs more bits.

## Result
FAAC vs Apple: n=45 curves, mean **+4.95 %**, median +3.42 %, 95 % CI [+0.48, +9.63].
Historical (encoder 367cf8dc, different env): +5.443 % [1.098, 9.907], 45 clips.
The gap persists on the frozen baseline, within noise of the historical value.

| rung | FAAC MOS @kbps | Apple MOS @kbps |
|---|---|---|
| 40 | 4.072 @40.3 | 4.136 @41.0 |
| 48 | 4.235 @48.3 | 4.271 @49.2 |
| 56 | 4.366 @56.2 | 4.417 @57.8 |
| 64 | 4.470 @64.0 | 4.545 @66.7 |

Worst clips: 35_SQAM_glockenspiel_cut +54.6 %, Girl_In_The_Fire +45.1 %, cst +22.6 %,
hrp +22.3 %, Severance +21.6 %. Best: glk -29.0 %, 9-Have-big-expensive-car -19.5 %.
Per-clip values: `bd_per_clip.json`; raw rows: `rows.json`.

## Limits
- Source WAVs: 40/49 reproduced to the manifest SHA-256 after rewriting only the
  `LIST/ISFT` header tag to `Lavf62.12.100` (`fixhdr.py`; PCM was already identical).
  The 9 SoundExpert 44.1->48 kHz clips (bah bas cst fms glk hrp lob mof qrt) did not
  match; by instruction they were assumed equivalent. Their manifest durations equal
  the 48 kHz durations, so they are 48 kHz originals with possibly different PCM.
- FFmpeg 6.1.1 generated the corpus; the Apple m4a files are historical bytes.
- Scored in this Linux env with a freshly built FAAD2; not the historical macOS decoder.
- 4 clips lacked overlapping curves (45 of 49 valid), as in the historical run.
- No FDK arm (no fdkaac encoder here) and no LC/other rungs yet.

## FDK arm (added)
Encoder: fdkaac 1.0.9 (nu774 `586bfb1`) + libfdk-aac 2.0.0 (`v2.0.0`, static), built from source;
command `fdkaac -p 5 -b N000 -o out.m4a in.wav` (the faac-benchmark HE v1 form). Streams
asserted HE-AAC, 48 kHz, 2 ch. Same FAAD2 decoder, scorer and BD method; `rows_fdk.json`,
`bd_per_clip_fdk.json`, `bd2.py <ref>`.

FAAC vs FDK: n=47 curves, mean **+6.46 %**, median +6.95 %, 95 % CI [+2.70, +10.23].
Historical: +6.933 % [3.119, 11.181], 47 clips. Mean MOS (FAAC / FDK): 40k 4.072/4.147,
48k 4.235/4.265, 56k 4.366/4.440, 64k 4.470/4.527. Worst for FAAC: SinceAlways +44.6 %,
trumpet +37.0 %, cst +29.8 %, Girl_In_The_Fire +27.3 %. Best: glk -30.3 %, Robots_old -15.4 %.
(The MOS lines printed by bd2.py label the reference arm "apple"; for this run it is FDK.)
FDK is therefore not an easier target at HE 40-64k here: the gap is slightly larger than Apple's.
Baseline for all arms: FreewareAdvancedAudio/faac master = 098e8900 (verified with ls-remote).

## Band-split diagnostic (first cause isolation; 4 worst clips)
`bands.py`: decode with the same FAAD2, zero-phase 8th-order Butterworth split of both
reference and decode into 0-4 / 4-8 / 8-12 / 12k+ Hz, zimtohrli MOS per band
(`bands.json`; rungs 48 and 64; clips cst, Girl_In_The_Fire, glockenspiel, SinceAlways).
This is a diagnostic proxy: band-limited MOS values are compressed and not comparable with
full-band MOS, and one HE crossover is not assumed. Only same-band differences between
encoders are read.

Findings (FAAC minus FDK, per band MOS):
- **48 kbps, 0-4 kHz:** cst -0.09, Girl -0.08, SinceAlways -0.17, glockenspiel -0.07.
- **48 kbps, 4-8 kHz:** cst -0.13, Girl -0.24, SinceAlways -0.18, glockenspiel -0.03.
- **48 kbps, 8-12 kHz:** -0.05, -0.14, -0.17, +0.03.
- **48 kbps, 12k+:** mixed: cst -0.05, Girl +0.17, SinceAlways -0.01, glockenspiel +0.12
  (FAAC ahead on two clips).
- At 64 kbps the 0-4 kHz deficit nearly closes (-0.03 to +0.02 on three clips) while 4-12 kHz
  stays behind (up to -0.19).
Reading: on these clips FAAC's deficit sits in the core band (below ~8 kHz) at 48 kbps and in
the 4-12 kHz region at 64 kbps; the top band (12k+), where SBR reconstructs everything, is not
consistently worse. This is consistent with the earlier S6-X/S9 conclusion (core coding
efficiency dominates), but it is four clips and a proxy: it neither proves the cause nor
covers SBR parameter quality below 12 kHz. Caveat from the rows: Apple spends up to 73 kbps on
Girl_In_The_Fire at the "64" rung (vs 64 for FAAC), so its +45 % BD-rate on that clip partly
reflects curve extrapolation.
