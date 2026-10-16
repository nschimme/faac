# Preregistration: LC at high bitrates vs Apple (written before scoring any variant)

Target (maintainer): beat Apple AAC at LC, high bitrates. Base: nschimme/faac `he-v1-retune` 19a4b70 (LC output is bit-identical to master).

## Measured before this note (49 clips, 48 kHz stereo, FAAD2, rows `rows_lc_faac_fdk_apple.json`)
- FAAC vs Apple BD-rate, 128/160/192 kbps: +6.89 % [+4.19, +9.78]; 96-192: +3.79 % [+0.15, +7.34]. FAAC vs FDK 128-192: -10.28 % [-13.31, -7.21].
- Mean MOS gap at equal nominal rung: -0.016 (128k), -0.014 (160k), -0.014 (192k); at 192k Apple wins 47 of 49 clips. Worst: velvet, take_your_finger, fms.
- Band split at 192 kbps (16 clips: 8 worst + 8 random, FAAD2): deficit is in 6-12 kHz (-0.015) and 12-16 kHz (-0.019); 0-6 kHz and 16 kHz+ are at parity.
- Defaults at 192k: LC + TNS + Mixed joint stereo (dynamic M/S and intensity stereo) + PNS, bandwidth 18750 Hz.

## Hypothesis
At high rates the parametric/lossy stereo and noise tools default-on in the 6-16 kHz region cost quality that the bits could buy: intensity stereo, PNS (and possibly TNS or the bandwidth).

## Screen (existing CLI switches; no code change), 49 clips, rungs 128/160/192, ABR
Arms: default; `--no-pns`; `--joint 1` (M/S only, no IS); `--no-pns --joint 1`; `--no-tns`; `-c 20000`.
Metric: per-clip PCHIP BD-rate vs default over the three rungs, clip-bootstrap 95 % CI, under FAAD2, FFmpeg and the FDK decoder.
Decision: an arm advances only if its CI excludes 0 favourably under all three decoders. It is a screen on the same corpus that located the gap;
anything that advances needs a held-out check and a rate-gated implementation (the switches are global today). The effect on Apple's gap is read
from the same rows (Apple LC 128/160/192 streams are packaged; 256/320 are not).
Not in scope: scalefactor search, loudness exponent, peak weight (other agents), window/M-S decisions copied from Apple (closed in S9).
