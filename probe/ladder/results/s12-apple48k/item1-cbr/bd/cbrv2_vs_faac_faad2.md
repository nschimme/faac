# cbrv2 vs faac  (decoder faad2, cbr/lc, rungs [128, 160, 192, 256, 320], metric mos)

BD-rate mean **-0.725%** (95% clip-bootstrap CI [-0.804, -0.643], 10000 resamples, seed 0), median -0.747%; valid curves 77/77, no valid curve: 0
PCHIP sign: 4 clips worse / 73 better. Linear (log-rate linear in MOS): **-0.753%** [-0.835, -0.668], n 77, 5 worse / 72 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 128 | 77 | 4.8692 | 4.8663 | +0.0029 | 128.10 | 127.95 | 4.697 | 4.707 | 70/0/7 |
| 160 | 77 | 4.9274 | 4.9258 | +0.0016 | 160.15 | 160.05 | 4.843 | 4.835 | 71/0/6 |
| 192 | 77 | 4.9559 | 4.9550 | +0.0009 | 192.22 | 192.15 | 4.896 | 4.895 | 72/0/5 |
| 256 | 77 | 4.9819 | 4.9814 | +0.0005 | 256.33 | 256.31 | 4.960 | 4.959 | 74/0/3 |
| 320 | 77 | 4.9909 | 4.9907 | +0.0002 | 320.47 | 320.47 | 4.981 | 4.981 | 73/0/4 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.0160 | 128 | 10xh_.wav |
| -0.0079 | 128 | mof.wav |
| -0.0031 | 128 | 18vr_.wav |
| -0.0022 | 128 | 9-Have-big-expensive-car.441.16b48k.wav |
| -0.0020 | 128 | 27-last-song-drums-and-trampets.441.16b48k.wav |
| -0.0018 | 160 | 27-last-song-drums-and-trampets.441.16b48k.wav |
| -0.0017 | 192 | 06 trumpet.wav |
| -0.0015 | 256 | 08 velvet.wav |
| -0.0014 | 128 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.0003 | 192 | 03 eig.wav |
| -0.0003 | 160 | 18vr_.wav |
| -0.0003 | 192 | Shinsho_pool_3min45_4min4.16b48k.wav |
| -0.0002 | 192 | fms.wav |
| -0.0002 | 128 | bah.wav |
| -0.0001 | 160 | 24td_.wav |
