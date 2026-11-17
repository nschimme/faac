# cbrv2 vs faac  (decoder ffmpeg, cbr/lc, rungs [128, 160, 192, 256, 320], metric mos)

BD-rate mean **-0.747%** (95% clip-bootstrap CI [-0.819, -0.673], 10000 resamples, seed 0), median -0.767%; valid curves 77/77, no valid curve: 0
PCHIP sign: 3 clips worse / 74 better. Linear (log-rate linear in MOS): **-0.770%** [-0.841, -0.693], n 77, 4 worse / 73 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 128 | 77 | 4.8689 | 4.8660 | +0.0030 | 128.10 | 127.95 | 4.698 | 4.704 | 72/0/5 |
| 160 | 77 | 4.9274 | 4.9257 | +0.0017 | 160.15 | 160.05 | 4.842 | 4.835 | 73/0/4 |
| 192 | 77 | 4.9559 | 4.9549 | +0.0010 | 192.22 | 192.15 | 4.896 | 4.894 | 74/0/3 |
| 256 | 77 | 4.9819 | 4.9814 | +0.0005 | 256.33 | 256.31 | 4.960 | 4.959 | 73/0/4 |
| 320 | 77 | 4.9909 | 4.9907 | +0.0002 | 320.47 | 320.47 | 4.981 | 4.981 | 72/1/4 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.0115 | 128 | 10xh_.wav |
| -0.0069 | 128 | mof.wav |
| -0.0024 | 128 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.0020 | 128 | 27-last-song-drums-and-trampets.441.16b48k.wav |
| -0.0019 | 160 | 27-last-song-drums-and-trampets.441.16b48k.wav |
| -0.0017 | 192 | 06 trumpet.wav |
| -0.0016 | 256 | 08 velvet.wav |
| -0.0004 | 128 | girl.16b48k.wav |
| -0.0003 | 192 | 03 eig.wav |
| -0.0002 | 160 | 24td_.wav |
| -0.0002 | 160 | 10xh_.wav |
| -0.0002 | 192 | Shinsho_pool_3min45_4min4.16b48k.wav |
| -0.0001 | 256 | qrt.wav |
| -0.0000 | 320 | 12-German-male-speech.441.16b48k.wav |
| -0.0000 | 320 | 26fr_.wav |
