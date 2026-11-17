# faac vs fdkref  (decoder fdkdec, cbr/he, rungs [32, 40, 48, 64], metric mos)

BD-rate mean **-5.512%** (95% clip-bootstrap CI [-8.871, -2.086], 10000 resamples, seed 0), median -6.248%; valid curves 36/36, no valid curve: 0
PCHIP sign: 11 clips worse / 25 better. Linear (log-rate linear in MOS): **-5.647%** [-8.964, -2.237], n 36, 10 worse / 26 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 32 | 36 | 3.8328 | 3.7543 | +0.0784 | 31.88 | 32.60 | 3.259 | 2.533 | 19/0/17 |
| 40 | 36 | 4.1264 | 4.0680 | +0.0584 | 39.90 | 40.57 | 3.493 | 3.199 | 23/0/13 |
| 48 | 36 | 4.3040 | 4.2358 | +0.0682 | 47.93 | 48.56 | 3.830 | 3.849 | 23/0/13 |
| 64 | 36 | 4.5234 | 4.5082 | +0.0152 | 63.97 | 64.61 | 4.085 | 4.001 | 22/0/14 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.3162 | 32 | Severance__1.31-1.51_.16b48k.wav |
| -0.3030 | 32 | 29ru_.wav |
| -0.2578 | 40 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.2335 | 48 | 01 castanets.wav |
| -0.2309 | 64 | 03 eig.wav |
| -0.2229 | 32 | 01 castanets.wav |
| -0.2149 | 32 | Changes.16b48k.wav |
| -0.2128 | 32 | 35_SQAM_glockenspiel_cut.16b48k.wav |
| -0.1954 | 64 | 01 castanets.wav |
| -0.1930 | 40 | 01 castanets.wav |
| -0.1930 | 40 | Asleep__4.11-4.31_.16b48k.wav |
| -0.1909 | 32 | Asleep__4.11-4.31_.16b48k.wav |
| -0.1823 | 32 | 24td_.wav |
| -0.1802 | 32 | 22ex_.wav |
| -0.1697 | 32 | 17qu_.wav |
