# faacps vs apple  (decoder faad2, cbr/hev2, rungs [24, 32, 48], metric mos)

BD-rate mean **-0.908%** (95% clip-bootstrap CI [-6.976, +5.759], 10000 resamples, seed 0), median -2.459%; valid curves 36/36, no valid curve: 0
PCHIP sign: 15 clips worse / 21 better. Linear (log-rate linear in MOS): **-0.947%** [-6.961, +5.752], n 36, 17 worse / 19 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 24 | 36 | 3.2689 | 3.2128 | +0.0562 | 24.06 | 24.27 | 2.484 | 2.347 | 20/0/16 |
| 32 | 36 | 3.4523 | 3.4296 | +0.0226 | 32.05 | 32.33 | 2.643 | 2.583 | 23/0/13 |
| 48 | 36 | 3.6693 | 3.6691 | +0.0002 | 48.10 | 48.43 | 2.734 | 2.739 | 19/0/17 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.5527 | 48 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.3779 | 32 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.3073 | 24 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.2634 | 24 | 06 trumpet.wav |
| -0.2597 | 32 | 29ru_.wav |
| -0.2204 | 48 | Hey Tonight.16b48k.wav |
| -0.2136 | 48 | 03 eig.wav |
| -0.2046 | 24 | Hey Tonight.16b48k.wav |
| -0.1707 | 24 | 25rb_.wav |
| -0.1625 | 24 | 29ru_.wav |
| -0.1458 | 32 | Hey Tonight.16b48k.wav |
| -0.1396 | 48 | 25rb_.wav |
| -0.1345 | 32 | 25rb_.wav |
| -0.1165 | 24 | 24td_.wav |
| -0.1111 | 32 | 06 trumpet.wav |
