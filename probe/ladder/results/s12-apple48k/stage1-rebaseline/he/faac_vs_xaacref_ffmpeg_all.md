# faac vs xaacref  (decoder ffmpeg, cbr/he, rungs [32, 40, 48, 64], metric mos)

BD-rate mean **-10.016%** (95% clip-bootstrap CI [-12.241, -7.824], 10000 resamples, seed 0), median -10.246%; valid curves 77/77, no valid curve: 0
PCHIP sign: 11 clips worse / 66 better. Linear (log-rate linear in MOS): **-9.551%** [-11.662, -7.490], n 77, 10 worse / 67 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 32 | 77 | 3.8428 | 3.7964 | +0.0465 | 31.88 | 32.51 | 3.101 | 2.813 | 43/0/34 |
| 40 | 77 | 4.1657 | 4.0881 | +0.0776 | 39.90 | 40.46 | 3.366 | 3.519 | 56/0/21 |
| 48 | 77 | 4.3459 | 4.2008 | +0.1451 | 47.93 | 48.43 | 3.722 | 3.804 | 68/0/9 |
| 64 | 77 | 4.5693 | 4.4976 | +0.0717 | 63.96 | 64.47 | 4.148 | 4.141 | 66/0/11 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.3900 | 32 | 18vr_.wav |
| -0.3720 | 32 | Severance__1.31-1.51_.16b48k.wav |
| -0.3450 | 40 | 13 fatboy_30sec.wav |
| -0.3399 | 32 | take_your_finger_frin_my_head.16b48k.wav |
| -0.3028 | 32 | 13 fatboy_30sec.wav |
| -0.2788 | 32 | 01 castanets.wav |
| -0.2743 | 32 | 24td_.wav |
| -0.2695 | 32 | mof.wav |
| -0.2463 | 32 | 29ru_.wav |
| -0.2423 | 40 | 18vr_.wav |
| -0.2342 | 32 | 14fe_.wav |
| -0.2326 | 32 | SinceAlways.16b48k.wav |
| -0.2029 | 64 | 03 eig.wav |
| -0.2018 | 32 | 10 Angels_Fall_First_ringing.wav |
| -0.2005 | 32 | hrp.wav |
