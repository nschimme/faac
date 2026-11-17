# faac vs xaacref  (decoder faad2, cbr/he, rungs [32, 40, 48, 64], metric mos)

BD-rate mean **-10.145%** (95% clip-bootstrap CI [-12.381, -7.961], 10000 resamples, seed 0), median -10.261%; valid curves 77/77, no valid curve: 0
PCHIP sign: 11 clips worse / 66 better. Linear (log-rate linear in MOS): **-9.693%** [-11.806, -7.640], n 77, 10 worse / 67 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 32 | 77 | 3.8431 | 3.7968 | +0.0464 | 31.88 | 32.51 | 3.132 | 2.811 | 45/0/32 |
| 40 | 77 | 4.1673 | 4.0881 | +0.0792 | 39.90 | 40.46 | 3.375 | 3.519 | 57/0/20 |
| 48 | 77 | 4.3485 | 4.2010 | +0.1475 | 47.93 | 48.43 | 3.718 | 3.804 | 68/0/9 |
| 64 | 77 | 4.5695 | 4.4978 | +0.0717 | 63.96 | 64.47 | 4.143 | 4.140 | 67/0/10 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.3797 | 32 | 18vr_.wav |
| -0.3792 | 32 | Severance__1.31-1.51_.16b48k.wav |
| -0.3403 | 32 | take_your_finger_frin_my_head.16b48k.wav |
| -0.3218 | 40 | 13 fatboy_30sec.wav |
| -0.2733 | 32 | 24td_.wav |
| -0.2682 | 32 | 01 castanets.wav |
| -0.2676 | 32 | mof.wav |
| -0.2606 | 32 | 29ru_.wav |
| -0.2319 | 32 | SinceAlways.16b48k.wav |
| -0.2300 | 40 | 18vr_.wav |
| -0.2208 | 32 | 14fe_.wav |
| -0.2113 | 64 | 03 eig.wav |
| -0.1990 | 32 | Changes.16b48k.wav |
| -0.1941 | 32 | TrosYGareg.16b48k.wav |
| -0.1879 | 32 | 13 fatboy_30sec.wav |
