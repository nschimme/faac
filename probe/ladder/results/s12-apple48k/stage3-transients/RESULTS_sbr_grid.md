# SBR onset-adaptive grid: HE-v1 CBR screen 32/40/48 (closed)

Screen narrowed from the prereg's scoring set to HE-v1 CBR 32/40/48 by the owner's delivery-mode instruction; 77
clips, FAAD2/FFmpeg/fdkdec, `FAAC_SBR_GRID=1` (frozen T=6, L=1) vs master CBR (`stage1-rebaseline/rows_he_cbr.jsonl`).
Table: `armcmp_sbrgrid_he_cbr.md`; rows: `rows_sbrgrid_he_cbr.jsonl`.

**Closed: fails the MOS gate.** Pre-echo drops at every rung under all three decoders (pooled NPER -0.3 dB at 32k,
-0.6..-0.9 dB at 40/48k, CIs below 0), but at 40k and 48k 21-28 clips per decoder fall more than 0.10 (worst 18vr_
-0.45, mof -0.34, Angels_Fall_First -0.33), mean MOS drops 0.076-0.085 and P1 drops 0.13-0.23; 32k is neutral (mean
+0.006..+0.008, 0-1 cells below -0.10, P1 -0.01..+0.02). Stereo fidelity and rate unchanged. The split matches
master's FIXFIX envelope count: 32 kbps stereo (16 kbps/ch) runs one envelope per frame, 40/48 (>= 17 kbps/ch) run
four (`SBR_FOUR_ENV_BITRATE_BPS`). The likely reading, not tested, is that the onset frames' variable grid gives up
the four-envelope time resolution master already had, so the HE gap to FDK at 40/48 is not a border-placement problem.
A rerun would need new causal evidence (e.g. a grid that keeps four envelopes around the onset border), not a retune.
