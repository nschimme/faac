# faacps vs xaacref  (decoder ffmpeg, cbr/hev2, rungs [24, 32, 48], metric mos)

BD-rate mean **+6.139%** (95% clip-bootstrap CI [-1.636, +14.270], 10000 resamples, seed 0), median +6.560%; valid curves 35/36, no valid curve: 1
PCHIP sign: 22 clips worse / 13 better. Linear (log-rate linear in MOS): **+3.856%** [-3.909, +11.869], n 35, 19 worse / 16 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 24 | 36 | 3.2990 | 3.3140 | -0.0150 | 24.06 | 24.26 | 2.511 | 2.518 | 17/0/19 |
| 32 | 36 | 3.4905 | 3.5343 | -0.0439 | 32.05 | 32.30 | 2.710 | 2.722 | 14/0/22 |
| 48 | 36 | 3.7231 | 3.6709 | +0.0522 | 48.10 | 48.36 | 2.835 | 2.797 | 26/0/10 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.5173 | 32 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.4877 | 48 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.3551 | 24 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.3325 | 24 | 03 eig.wav |
| -0.2720 | 32 | 29ru_.wav |
| -0.2598 | 24 | Hey Tonight.16b48k.wav |
| -0.2562 | 24 | 29ru_.wav |
| -0.2374 | 24 | 35_SQAM_glockenspiel_cut.16b48k.wav |
| -0.2322 | 24 | 01 castanets.wav |
| -0.2275 | 24 | 06 trumpet.wav |
| -0.2211 | 32 | 01 castanets.wav |
| -0.2134 | 32 | 24td_.wav |
| -0.2117 | 32 | 03 eig.wav |
| -0.1945 | 24 | 10xh_.wav |
| -0.1933 | 24 | Changes.16b48k.wav |

No valid curve: 27-last-song-drums-and-trampets.441.16b48k.wav (no valid curve)
