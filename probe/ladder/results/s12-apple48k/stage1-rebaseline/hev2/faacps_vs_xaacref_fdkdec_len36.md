# faacps vs xaacref  (decoder fdkdec, cbr/hev2, rungs [24, 32, 48], metric mos)

BD-rate mean **+5.658%** (95% clip-bootstrap CI [-1.986, +13.707], 10000 resamples, seed 0), median +6.417%; valid curves 35/36, no valid curve: 1
PCHIP sign: 21 clips worse / 14 better. Linear (log-rate linear in MOS): **+3.429%** [-4.210, +11.374], n 35, 20 worse / 15 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 24 | 36 | 3.2996 | 3.3143 | -0.0147 | 24.06 | 24.26 | 2.507 | 2.517 | 16/0/20 |
| 32 | 36 | 3.4941 | 3.5346 | -0.0404 | 32.05 | 32.30 | 2.704 | 2.722 | 15/0/21 |
| 48 | 36 | 3.7270 | 3.6705 | +0.0565 | 48.10 | 48.36 | 2.836 | 2.795 | 26/0/10 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.5021 | 32 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.4860 | 48 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.3651 | 24 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.3473 | 24 | 03 eig.wav |
| -0.2633 | 32 | 29ru_.wav |
| -0.2554 | 24 | 29ru_.wav |
| -0.2319 | 24 | Changes.16b48k.wav |
| -0.2287 | 24 | 06 trumpet.wav |
| -0.2212 | 24 | 10xh_.wav |
| -0.2119 | 32 | 24td_.wav |
| -0.2102 | 32 | 01 castanets.wav |
| -0.2001 | 32 | 03 eig.wav |
| -0.1926 | 24 | 35_SQAM_glockenspiel_cut.16b48k.wav |
| -0.1906 | 48 | Hey Tonight.16b48k.wav |
| -0.1839 | 24 | Hey Tonight.16b48k.wav |

No valid curve: 27-last-song-drums-and-trampets.441.16b48k.wav (no valid curve)
