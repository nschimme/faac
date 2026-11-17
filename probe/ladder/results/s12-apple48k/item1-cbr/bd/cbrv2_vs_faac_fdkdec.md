# cbrv2 vs faac  (decoder fdkdec, cbr/lc, rungs [128, 160, 192, 256, 320], metric mos)

BD-rate mean **-0.760%** (95% clip-bootstrap CI [-0.841, -0.675], 10000 resamples, seed 0), median -0.793%; valid curves 77/77, no valid curve: 0
PCHIP sign: 4 clips worse / 73 better. Linear (log-rate linear in MOS): **-0.780%** [-0.864, -0.691], n 77, 4 worse / 73 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 128 | 77 | 4.8688 | 4.8658 | +0.0030 | 128.10 | 127.95 | 4.694 | 4.706 | 71/0/6 |
| 160 | 77 | 4.9273 | 4.9255 | +0.0018 | 160.15 | 160.05 | 4.843 | 4.834 | 74/0/3 |
| 192 | 77 | 4.9559 | 4.9549 | +0.0010 | 192.22 | 192.15 | 4.896 | 4.894 | 72/0/5 |
| 256 | 77 | 4.9819 | 4.9814 | +0.0005 | 256.33 | 256.31 | 4.960 | 4.959 | 73/0/4 |
| 320 | 77 | 4.9909 | 4.9907 | +0.0002 | 320.47 | 320.47 | 4.981 | 4.981 | 73/0/4 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.0178 | 128 | 10xh_.wav |
| -0.0073 | 128 | mof.wav |
| -0.0045 | 128 | 27-last-song-drums-and-trampets.441.16b48k.wav |
| -0.0022 | 128 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.0015 | 256 | 08 velvet.wav |
| -0.0014 | 160 | 27-last-song-drums-and-trampets.441.16b48k.wav |
| -0.0013 | 128 | 14fe_.wav |
| -0.0011 | 192 | 06 trumpet.wav |
| -0.0004 | 192 | 03 eig.wav |
| -0.0002 | 192 | Shinsho_pool_3min45_4min4.16b48k.wav |
| -0.0001 | 128 | 12-German-male-speech.441.16b48k.wav |
| -0.0001 | 160 | glk.wav |
| -0.0001 | 256 | qrt.wav |
| -0.0000 | 320 | 12-German-male-speech.441.16b48k.wav |
| -0.0000 | 320 | 26fr_.wav |
