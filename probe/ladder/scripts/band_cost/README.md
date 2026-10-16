Per-band cost comparison, FAAC vs Apple (read-only, black-box: bitstream syntax only).
- `faad_banddump.patch`: apply to nschimme/faac `libfaad` branch (80086fb); with `FAAD_BANDDUMP=<file>` the decoder writes one `S` line per ICS and one `B` line per band: group, sfb, start, end, codebook, scalefactor, scalefactor bits, spectral bits, nonzero lines, sum |q|, max |q|.
- `bandstats.py <dumps>`: per frequency region bands, bits per line, nonzero share, codebook usage (long and short windows; the short-window per-line columns are not normalised, ignore them).
- `sfcmp.py <clip>`: mean scalefactor per sfb, FAAC minus Apple (expects `<clip>.faac.dump` and `<clip>.apple.dump`).
Scoring: `score_ladder.py` (encode, ffmpeg decode, Zimtohrli via faac-benchmark `phase2_mos.score_wav_pair`; paths hard-coded to /tmp/claude, edit them) and `compare_bd.py` (PCHIP BD-rate via faac-benchmark `scripts/bd_rate.py`, clip bootstrap, worst cell). `tilt_experiment.patch` is the tilt test.
Inputs used: Apple `ref/apple-s11-367cf8dc/lc/160k`, FAAC master `-b 160` on the first 10 s of the PMLT2014 clip (`ffmpeg -t 10`; the Apple streams are 10.048 s).
