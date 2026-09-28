# 48 kHz crossover probe: measured result

49 clips, 40/48/56/64 kbps, HE-AAC v1 stereo. All 980 bitstream decodes (N15/N13/G13/F, two decoders) were serial. S13 is a decode-domain hybrid.

## Table A: MOS

Each MOS cell is raw / bit-adjusted. Δ and W/L use raw MOS. W/L counts use ±0.02 per clip; ties omitted. Bit adjustment charges the clip-specific N15 MOS-per-bit slope estimated from adjacent rates against the arm’s total ADTS bytes. S13 uses N13’s stream size as its nominal bit price.

| kbps | decoder | arm | MOS raw / adjusted | Δ vs N15 (W/L) | Δ vs F (W/L) |
|---:|:---|:---|---:|---:|---:|
| 40 | ff | N15 | 4.0460 / 4.0460 | +0.0000 (0/0) | -0.0673 (12/31) |
| 40 | ff | N13 | 3.9608 / 3.9647 | -0.0852 (5/39) | -0.1525 (6/42) |
| 40 | ff | G13 | 3.9440 / 3.9474 | -0.1021 (4/43) | -0.1694 (4/44) |
| 40 | ff | S13 | 4.0174 / 4.0213 | -0.0286 (21/22) | -0.0960 (8/38) |
| 40 | ff | F | 4.1134 / 4.1220 | +0.0673 (31/12) | +0.0000 (0/0) |
| 40 | fdk | N15 | 4.0384 / 4.0384 | +0.0000 (0/0) | -0.0446 (14/33) |
| 40 | fdk | N13 | 3.9124 / 3.9162 | -0.1260 (3/44) | -0.1706 (3/44) |
| 40 | fdk | G13 | 3.8879 / 3.8913 | -0.1505 (1/45) | -0.1951 (3/45) |
| 40 | fdk | S13 | 3.9915 / 3.9953 | -0.0469 (17/26) | -0.0915 (7/34) |
| 40 | fdk | F | 4.0830 / 4.0914 | +0.0446 (33/14) | +0.0000 (0/0) |
| 48 | ff | N15 | 4.1931 / 4.1931 | +0.0000 (0/0) | -0.0416 (16/27) |
| 48 | ff | N13 | 4.1035 / 4.1076 | -0.0896 (5/39) | -0.1312 (5/42) |
| 48 | ff | G13 | 4.0997 / 4.1034 | -0.0933 (3/40) | -0.1349 (4/41) |
| 48 | ff | S13 | 4.1580 / 4.1621 | -0.0351 (15/23) | -0.0767 (8/37) |
| 48 | ff | F | 4.2347 / 4.2445 | +0.0416 (27/16) | +0.0000 (0/0) |
| 48 | fdk | N15 | 4.1820 / 4.1820 | +0.0000 (0/0) | -0.0342 (15/28) |
| 48 | fdk | N13 | 4.0605 / 4.0646 | -0.1215 (3/42) | -0.1557 (4/42) |
| 48 | fdk | G13 | 4.0488 / 4.0525 | -0.1332 (1/46) | -0.1674 (3/45) |
| 48 | fdk | S13 | 4.1399 / 4.1440 | -0.0422 (14/26) | -0.0764 (8/36) |
| 48 | fdk | F | 4.2162 / 4.2259 | +0.0342 (28/15) | +0.0000 (0/0) |
| 56 | ff | N15 | 4.3255 / 4.3255 | +0.0000 (0/0) | -0.0995 (6/39) |
| 56 | ff | N13 | 4.2837 / 4.2863 | -0.0419 (6/33) | -0.1414 (1/45) |
| 56 | ff | G13 | 4.2830 / 4.2854 | -0.0425 (6/33) | -0.1420 (1/44) |
| 56 | ff | S13 | 4.3447 / 4.3474 | +0.0192 (24/14) | -0.0803 (7/38) |
| 56 | ff | F | 4.4250 / 4.4317 | +0.0995 (39/6) | +0.0000 (0/0) |
| 56 | fdk | N15 | 4.3130 / 4.3130 | +0.0000 (0/0) | -0.0896 (5/38) |
| 56 | fdk | N13 | 4.2477 / 4.2503 | -0.0654 (4/38) | -0.1550 (1/46) |
| 56 | fdk | G13 | 4.2338 / 4.2362 | -0.0792 (4/41) | -0.1688 (1/47) |
| 56 | fdk | S13 | 4.3252 / 4.3279 | +0.0122 (22/15) | -0.0774 (7/38) |
| 56 | fdk | F | 4.4026 / 4.4094 | +0.0896 (38/5) | +0.0000 (0/0) |
| 64 | ff | N15 | 4.4302 / 4.4302 | +0.0000 (0/0) | -0.0815 (4/34) |
| 64 | ff | N13 | 4.4007 / 4.4019 | -0.0295 (3/22) | -0.1110 (1/41) |
| 64 | ff | G13 | 4.4001 / 4.4009 | -0.0301 (5/28) | -0.1116 (1/41) |
| 64 | ff | S13 | 4.4438 / 4.4449 | +0.0135 (21/10) | -0.0679 (6/32) |
| 64 | ff | F | 4.5117 / 4.5150 | +0.0815 (34/4) | +0.0000 (0/0) |
| 64 | fdk | N15 | 4.4217 / 4.4217 | +0.0000 (0/0) | -0.0798 (7/34) |
| 64 | fdk | N13 | 4.3843 / 4.3855 | -0.0373 (5/29) | -0.1172 (1/43) |
| 64 | fdk | G13 | 4.3729 / 4.3737 | -0.0488 (4/31) | -0.1286 (1/43) |
| 64 | fdk | S13 | 4.4362 / 4.4374 | +0.0145 (22/9) | -0.0653 (7/33) |
| 64 | fdk | F | 4.5015 / 4.5051 | +0.0798 (34/7) | +0.0000 (0/0) |

## Table B: bit allocation

Bits/frame are for the full stereo access unit. Env/band divides all envelope bits by the number of coded envelope bands across frames and channels, using each envelope’s high/low resolution flag. Noise-band counts differ by arm. Grid includes the header. S13 has no independent bitstream.

| kbps | arm | core bits/frame | SBR bits/frame | grid/env/noise/other bits per coded band | coded bands/frame | grid/env/noise/other bits per frame |
|---:|:---|---:|---:|:---|---:|:---|
| 40 | N15 | 1601.7 | 85.0 | 1.240/1.779/0.178/0.354 | 23.9 | 29.7/42.6/4.3/8.5 |
| 40 | N13 | 1544.9 | 131.6 | 0.706/1.776/0.089/0.178 | 47.9 | 33.8/85.1/4.3/8.5 |
| 40 | G13 | 1532.0 | 145.7 | 0.955/2.338/0.121/0.238 | 40.1 | 38.1/93.5/4.9/9.2 |
| 40 | F | 1496.1 | 162.5 | 0.998/2.325/0.344/0.431 | 40.2 | 39.8/92.3/13.7/16.7 |
| 48 | N15 | 1912.6 | 113.4 | 0.784/1.738/0.107/0.213 | 39.9 | 31.3/69.4/4.3/8.5 |
| 48 | N13 | 1900.6 | 114.9 | 0.788/1.772/0.107/0.212 | 39.9 | 31.4/70.7/4.3/8.5 |
| 48 | G13 | 1888.1 | 128.6 | 1.134/2.374/0.145/0.291 | 32.8 | 36.9/77.7/4.8/9.1 |
| 48 | F | 1847.3 | 145.3 | 1.204/2.368/0.411/0.495 | 32.8 | 39.2/76.9/13.5/15.7 |
| 56 | N15 | 2246.4 | 113.4 | 0.784/1.738/0.107/0.213 | 39.9 | 31.3/69.4/4.3/8.5 |
| 56 | N13 | 2220.6 | 131.2 | 0.706/1.766/0.089/0.178 | 47.9 | 33.8/84.6/4.3/8.5 |
| 56 | G13 | 2209.1 | 143.6 | 0.956/2.308/0.120/0.240 | 39.8 | 37.9/91.8/4.8/9.1 |
| 56 | F | 2168.5 | 161.1 | 1.013/2.321/0.337/0.419 | 39.9 | 40.1/91.5/13.4/16.1 |
| 64 | N15 | 2577.7 | 113.4 | 0.784/1.738/0.107/0.213 | 39.9 | 31.3/69.4/4.3/8.5 |
| 64 | N13 | 2537.9 | 149.1 | 0.641/1.800/0.076/0.152 | 55.9 | 35.8/100.6/4.3/8.5 |
| 64 | G13 | 2523.6 | 164.4 | 0.838/2.415/0.103/0.207 | 46.1 | 38.5/112.0/4.8/9.1 |
| 64 | F | 2449.6 | 217.3 | 1.053/2.969/0.283/0.466 | 46.2 | 47.8/136.0/13.0/20.6 |

## Table C: stereo coherence error

Benchmark phase-3 per-frame coherence error; lower is better.

| kbps | decoder | N15 | N13 | G13 | S13 | F |
|---:|:---|---:|---:|---:|---:|---:|
| 40 | ff | 0.0392 (n=49) | 0.0421 (n=49) | 0.0418 (n=49) | 0.0403 (n=49) | 0.0881 (n=49) |
| 40 | fdk | 0.0389 (n=49) | 0.0422 (n=49) | 0.0419 (n=49) | 0.0403 (n=49) | 0.0191 (n=49) |
| 48 | ff | 0.0208 (n=49) | 0.0206 (n=49) | 0.0207 (n=49) | 0.0197 (n=49) | 0.0882 (n=49) |
| 48 | fdk | 0.0208 (n=49) | 0.0207 (n=49) | 0.0207 (n=49) | 0.0196 (n=49) | 0.0150 (n=49) |
| 56 | ff | 0.0181 (n=49) | 0.0183 (n=49) | 0.0183 (n=49) | 0.0175 (n=49) | 0.0890 (n=49) |
| 56 | fdk | 0.0180 (n=49) | 0.0183 (n=49) | 0.0183 (n=49) | 0.0176 (n=49) | 0.0125 (n=49) |
| 64 | ff | 0.0162 (n=49) | 0.0163 (n=49) | 0.0163 (n=49) | 0.0155 (n=49) | 0.0867 (n=49) |
| 64 | fdk | 0.0160 (n=49) | 0.0161 (n=49) | 0.0161 (n=49) | 0.0156 (n=49) | 0.0103 (n=49) |

## Interpretation

40 kbps ff: S13−F -0.0960; N13→G13 -0.0169; N13→S13 +0.0566; grid fraction -29.8%.
40 kbps fdk: S13−F -0.0915; N13→G13 -0.0245; N13→S13 +0.0791; grid fraction -31.0%.
48 kbps ff: S13−F -0.0767; N13→G13 -0.0037; N13→S13 +0.0545; grid fraction -6.9%.
48 kbps fdk: S13−F -0.0764; N13→G13 -0.0117; N13→S13 +0.0794; grid fraction -14.7%.
56 kbps ff: S13−F -0.0803; N13→G13 -0.0006; N13→S13 +0.0610; grid fraction -1.0%.
56 kbps fdk: S13−F -0.0774; N13→G13 -0.0138; N13→S13 +0.0776; grid fraction -17.8%.
64 kbps ff: S13−F -0.0679; N13→G13 -0.0006; N13→S13 +0.0430; grid fraction -1.5%.
64 kbps fdk: S13−F -0.0653; N13→G13 -0.0115; N13→S13 +0.0519; grid fraction -22.1%.

The hybrid is a measurement ceiling for replacing only the decoded high band at the matched crossover; the FIR transition and encoder phase may affect its score. Fractions with a near-zero or negative N13→S13 gap should not be interpreted as progress toward a positive ceiling.

## Gates and provenance

Known arm difference: fdk noise_bands is 2 at 40 kbps and 3 at 48/56/64; FAAC signals 0 (one noise band) at all rates. This exception is permitted. The master, high and low decoder-derived edge arrays match exactly at each rate; the noise tables intentionally differ. All other SBR header fields match.

| kbps | start | stop | xover | freq_scale | alter | amp_res | noise_bands F/N13 | kx | full master edges |
|---:|---:|---:|---:|---:|---:|---:|:---|---:|:---|
| 40 | 10 | 9 | 0 | 2 | 1 | 1 | 2/0 | 18 | 18 19 20 21 23 25 27 29 31 33 36 40 45 |
| 48 | 12 | 9 | 0 | 2 | 1 | 1 | 3/0 | 22 | 22 23 25 27 29 31 33 36 39 42 45 |
| 56 | 13 | 10 | 0 | 1 | 1 | 1 | 3/0 | 24 | 24 25 26 28 30 32 34 36 38 40 43 46 49 |
| 64 | 14 | 12 | 0 | 1 | 1 | 1 | 3/0 | 27 | 27 28 30 32 34 36 38 40 42 44 47 50 53 56 59 |

Five-clip off-knob identity passed 20/20 SHA-256 comparisons (results in `xover_identity/results.csv`). Five-clip, four-rate N13 recombination max absolute MOS bias: 0.0127. Every G13 donor-grid match was 100% on both channels. Every N13/G13 FAAD decode had zero concealment and zero non-END termination; FFmpeg strict decodes succeeded. On every clip/rate the C-record mean max_sfb dropped from N15 to N13, following the lower kx. FAAC and fdk alignment uses the established 33-sample donor pad and FAAC frame n ↔ fdk n+1.

Commands: `CCACHE_DISABLE=1 meson compile -C build`; `/Users/nschimme/gitprojects/faac-benchmark/.venv/bin/python probe/xover_identity.py`; `/Users/nschimme/gitprojects/faac-benchmark/.venv/bin/python probe/xover_run.py --limit 5`; `/Users/nschimme/gitprojects/faac-benchmark/.venv/bin/python probe/xover_run.py`; `/Users/nschimme/gitprojects/faac-benchmark/.venv/bin/python probe/xover_coherence.py`; `/Users/nschimme/gitprojects/faac-benchmark/.venv/bin/python probe/xover_analyze.py`. Run script `xover_run.py` contains the exact serial encode, decode, alignment, bandswap, and score commands. Probe source diff is `xover.patch`; FAAD3 T-record instrumentation is `faad-xover.patch`. No fdk-aac source was read. No commit or push.
