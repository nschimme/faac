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
