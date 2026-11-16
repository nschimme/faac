# S12 Stage 2 prereg: intensity stereo on master (written before scoring)

## What master does (read from `libfaac/stereo.c` at origin/master 3a656f7, before any measurement)
`StereoConfigure` in the default mode (`--joint auto`, JOINT_MIXED), with r = configured bits per channel:
- r >= 40 kbps/ch (not starved): long windows get `msEnd = all bands`. In `process_cpe` a band below `msEnd` takes the
  M/S test and then `continue`s, so **long windows never take intensity stereo** from 80 kbps stereo up.
- r >= 80 kbps/ch (160 kbps stereo and up): short windows also get every band M/S, so **no intensity stereo at all**.
  The only difference to `--joint ms` there is the M/S bias (k = 1.5 from 64 kbps/ch).
- 40 <= r < 80 kbps/ch (80-158 kbps stereo): short windows take M/S below 2 kHz and **intensity above**.
- VBR (r = 0, LC): long windows all-M/S; short windows M/S below 2 kHz and intensity above, at every quality.
- r < 40 kbps/ch: starved tiers (intensity on long windows too); out of scope here (Stage 4).
So the old `--joint 1` -1.58 % is spent at >= 160 kbps ABR/CBR except the bias term; what remains is short-window IS at
96/128 kbps ABR/CBR and at every VBR quality.

## Predictions (stated before scoring)
P1. ABR/CBR 160-320: default vs `--joint ms` differ only by the bias; |BD-rate| < 0.3 % or CI spans 0.
P2. ABR/CBR 96/128 and VBR: any `--joint ms` gain comes from short frames only; a per-clip gain correlates with the
    clip's short-window share (Spearman, reported, not gated).

## Screen (existing CLI switches only)
- Arms: default (auto), `--joint ms`, `--joint lr`, `--joint is`. LC 48 kHz stereo, all 77 clips.
- Rungs: ABR and CBR 96/128/160/192/256/320; VBR at the settings Stage 1 maps to those rates (plus 96 kbps if the
  benchmark scores it). Decoders FAAD2, FFmpeg, fdkdec. Rate axis is realized kbps.
- FAAC vs FAAC: BD-rate of each arm vs default with clip-bootstrap 95 % CI over 96-160, 160-320 and 96-320, per mode,
  per decoder; per-rung mean dMOS and mean dkbps (realized); worst clip x rung table first (gate: any cell < -0.10 fails).
- Decision dumps (`scripts/s12/decstats.py`): share of IS / M/S / L/R / PNS / zero bands by frequency region x window
  type for FAAC default, FAAC `--joint ms`, Apple and FDK at 128 and 192 (and VBR mid setting for FAAC/FDK).

## Advance rule
A follow-up scratch patch (rate-gated or better short-window IS decision, env-driven) gets its own prereg only if
`--joint ms` vs default has a BD-rate CI wholly below 0 under all three decoders in at least one mode's 96-160 range,
or in VBR, with no clip x rung cell below -0.10. Otherwise the stage closes as a negative result. The short-M/S
(`MS_SHORT_BITRATE`) and bias gates themselves are closed and are not swept.
