# Stage 3 result: short-window TNS v1 (all short windows), LC ABR 32-128 (HE part pending)

Prereg: `PREREG.md`. Arm `stns` = `scripts/s12/stns_scratch.patch` with `FAAC_STNS=1` (order 4, gain bar 1.4, from 1.5 kHz,
every EIGHT_SHORT window). Control = master (same patched binary unset is payload-identical). Rows `rows_s3_lc_abr.jsonl`;
table `stns_lc_armcmp.md` (`scripts/s12/armcmp.py`: pooled NPER difference with a 4000-resample clip bootstrap).

## Worst cells first (FAAD2; FFmpeg and fdkdec agree)
Cells below -0.10: 7 / 7 / 5 / 3 / 2 at 32 / 40 / 64 / 96 / 128 kbps. Worst: 13 fatboy -0.59 (96k), -0.54 (64k), -0.37
(40k), -0.34 (128k); bas.wav -0.22 (64k), -0.20 (96k); 24-Greensleeves -0.18 (96k), -0.16 (128k); 14fe_ -0.14 (64k).
Mean MOS -0.011 to -0.030 per rung. **Fails the -0.10 gate at every rung.**

## NPER (pooled excess pre-echo, dB; arm - control, 95 % CI)
| rung | control | arm | difference |
|---|---|---|---|
| 32 | 17.75 | 13.98 | -3.78 [-4.62, -1.67] |
| 40 | 14.30 | 10.95 | -3.35 [-4.29, -1.08] |
| 64 | 9.90 | 7.93 | -1.97 [-2.61, -0.65] |
| 96 | 7.97 | 6.50 | -1.48 [-2.01, -0.52] |
| 128 | 5.12 | 4.12 | -1.00 [-1.26, -0.32] |
All three decoders within 0.1 dB of these; every CI excludes 0. At 128k the arm (4.12) passes FDK (4.98) and nears
Apple (3.85); at 96k 6.50 vs Apple 6.38 (gist). Stereo fidelity unchanged (within +-0.001). Rate +0.0..+0.2 %.

## Verdict
**Negative as configured** (MOS gate fails), but the causal link is confirmed: TNS on short windows removes 1-4 dB of
FAAC's excess pre-echo. The losers are not only mostly-short clips (bas.wav and 14fe_ are 12-18 % short; 13 fatboy
73 %), i.e. the damage comes from shaping noise in short windows that hold no attack. Next arm (v2, own prereg): TNS only
on the attack window(s) of a short frame. Cost reminder: v1 +25.6 % Ir on a 95 %-short clip, +1664 B.
