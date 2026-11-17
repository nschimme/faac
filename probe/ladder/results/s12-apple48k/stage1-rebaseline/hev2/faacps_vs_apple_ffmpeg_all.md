# faacps vs apple  (decoder ffmpeg, cbr/hev2, rungs [24, 32, 48], metric mos)

BD-rate mean **+1.312%** (95% clip-bootstrap CI [-3.503, +6.119], 10000 resamples, seed 0), median +1.526%; valid curves 76/77, no valid curve: 1
PCHIP sign: 42 clips worse / 34 better. Linear (log-rate linear in MOS): **+1.241%** [-3.430, +5.880], n 76, 44 worse / 32 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 24 | 77 | 3.3087 | 3.2832 | +0.0255 | 24.07 | 24.30 | 2.220 | 2.054 | 35/0/42 |
| 32 | 77 | 3.4934 | 3.4736 | +0.0198 | 32.06 | 32.36 | 2.349 | 2.145 | 33/0/44 |
| 48 | 77 | 3.7462 | 3.7269 | +0.0193 | 48.11 | 48.43 | 2.534 | 2.512 | 42/0/35 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.5293 | 48 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.5005 | 32 | 10 Angels_Fall_First_ringing.wav |
| -0.4895 | 24 | 10 Angels_Fall_First_ringing.wav |
| -0.3727 | 32 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.3262 | 24 | Hey Tonight.16b48k.wav |
| -0.3059 | 24 | 06 trumpet.wav |
| -0.2935 | 24 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.2800 | 24 | Berling Drug.wav |
| -0.2689 | 32 | 29ru_.wav |
| -0.2505 | 24 | Coral.16b48k.wav |
| -0.2435 | 32 | Coral.16b48k.wav |
| -0.2347 | 24 | SinceAlways.16b48k.wav |
| -0.2298 | 32 | ExitMusic.16b48k.wav |
| -0.2285 | 24 | TrosYGareg.16b48k.wav |
| -0.2226 | 48 | Hey Tonight.16b48k.wav |

No valid curve: mof.wav (no valid curve)
