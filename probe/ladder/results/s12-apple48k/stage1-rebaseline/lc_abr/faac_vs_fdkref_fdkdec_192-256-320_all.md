# faac vs fdkref  (decoder fdkdec, abr/lc, rungs [192, 256, 320], metric mos)

BD-rate mean **-21.587%** (95% clip-bootstrap CI [-24.702, -18.413], 10000 resamples, seed 0), median -22.833%; valid curves 49/77, no valid curve: 28
PCHIP sign: 1 clips worse / 48 better. Linear (log-rate linear in MOS): **-20.983%** [-23.987, -17.949], n 49, 1 worse / 48 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 192 | 77 | 4.9552 | 4.9266 | +0.0286 | 189.41 | 192.74 | 4.905 | 4.828 | 74/0/3 |
| 256 | 77 | 4.9815 | 4.9524 | +0.0291 | 252.38 | 256.90 | 4.963 | 4.886 | 76/0/1 |
| 320 | 77 | 4.9907 | 4.9598 | +0.0309 | 314.28 | 321.11 | 4.981 | 4.896 | 76/0/1 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.0234 | 192 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0055 | 192 | 21-classic.441.16b48k.wav |
| -0.0030 | 256 | 21-classic.441.16b48k.wav |
| -0.0021 | 192 | TrosYGareg.16b48k.wav |
| -0.0003 | 320 | 21-classic.441.16b48k.wav |
| +0.0003 | 192 | fms.wav |
| +0.0006 | 256 | TrosYGareg.16b48k.wav |
| +0.0023 | 192 | glk.wav |
| +0.0023 | 256 | Shinsho_pool_3min45_4min4.16b48k.wav |
| +0.0025 | 320 | TrosYGareg.16b48k.wav |
| +0.0026 | 256 | Berling Drug.wav |
| +0.0028 | 192 | Berling Drug.wav |
| +0.0033 | 192 | Severance__1.31-1.51_.16b48k.wav |
| +0.0035 | 256 | Only Time.16b48k.wav |
| +0.0035 | 256 | glk.wav |

No valid curve: 03 eig.wav (no valid curve), 04 Bachpsichord.wav (no valid curve), 06 trumpet.wav (no valid curve), 10xh_.wav (no valid curve), 11 You look good to me.wav (no valid curve), 11ff_.wav (no valid curve), 12-German-male-speech.441.16b48k.wav (no valid curve), 16mb_.wav (no valid curve), 17qu_.wav (no valid curve), 22ex_.wav (no valid curve), 23ht_.wav (no valid curve), 24td_.wav (no valid curve), 26fr_.wav (no valid curve), 29ru_.wav (no valid curve), 35_SQAM_glockenspiel_cut.16b48k.wav (no valid curve), 4-Sound-English-male.441.16b48k.wav (no valid curve), Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav (no valid curve), ExitMusic.16b48k.wav (no valid curve), Hey Tonight.16b48k.wav (no valid curve), Last_Of_The_Mohicanz__Sample_.16b48k.wav (no valid curve), NewYorkCity.16b48k.wav (no valid curve), Robots_old.16b48k.wav (no valid curve), With Love (Outro).16b48k.wav (no valid curve), clapton_44k.16b48k.wav (no valid curve), girl.16b48k.wav (no valid curve), glk.wav (no valid curve), hrp.wav (no valid curve), lob.wav (no valid curve)
