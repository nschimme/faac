# faacps vs apple  (decoder faad2, cbr/hev2, rungs [24, 32, 48], metric mos)

BD-rate mean **+0.889%** (95% clip-bootstrap CI [-4.242, +6.160], 10000 resamples, seed 0), median -0.100%; valid curves 76/77, no valid curve: 1
PCHIP sign: 38 clips worse / 38 better. Linear (log-rate linear in MOS): **+1.005%** [-3.837, +6.103], n 76, 41 worse / 35 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 24 | 77 | 3.2889 | 3.2573 | +0.0317 | 24.07 | 24.30 | 2.164 | 2.074 | 38/0/39 |
| 32 | 77 | 3.4640 | 3.4405 | +0.0234 | 32.06 | 32.36 | 2.287 | 2.176 | 41/0/36 |
| 48 | 77 | 3.7014 | 3.6878 | +0.0135 | 48.11 | 48.43 | 2.472 | 2.434 | 42/0/35 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.5527 | 48 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.4866 | 24 | 10 Angels_Fall_First_ringing.wav |
| -0.4853 | 32 | 10 Angels_Fall_First_ringing.wav |
| -0.3779 | 32 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.3270 | 48 | lob.wav |
| -0.3073 | 24 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.2640 | 24 | hrp.wav |
| -0.2634 | 24 | 06 trumpet.wav |
| -0.2597 | 32 | 29ru_.wav |
| -0.2473 | 24 | Berling Drug.wav |
| -0.2354 | 24 | SinceAlways.16b48k.wav |
| -0.2204 | 48 | Hey Tonight.16b48k.wav |
| -0.2201 | 32 | ExitMusic.16b48k.wav |
| -0.2136 | 48 | 03 eig.wav |
| -0.2134 | 32 | Coral.16b48k.wav |

No valid curve: mof.wav (no valid curve)
