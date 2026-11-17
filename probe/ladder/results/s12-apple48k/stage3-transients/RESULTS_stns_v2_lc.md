# Stage 3: short-window TNS v2 (attack window only), LC ABR 32-128 — closed negative

Rows `rows_s3_lc_abr.jsonl` (arm stns2), table `stns2_lc_armcmp.md`. Fails the prereg's MOS gate at every rung (cells
below -0.10: 2 / 4 / 1 / 2 / 2 at 32 / 40 / 64 / 96 / 128 under FAAD2; worst 13 fatboy -0.435 @96, -0.255 @64, TrosYGareg
-0.286 @40, Through The Fire -0.216 @32, Greensleeves -0.169 @96; mean MOS -0.006..-0.017), and keeps only part of v1's
pre-echo gain: pooled NPER -1.71 / -1.53 / -0.72 / -0.30 / -0.14 dB (v1 -3.78 / -3.35 / -1.97 / -1.48 / -1.00; share kept
45 / 46 / 37 / 20 / 14 %), CIs spanning 0 at 96 and 128. Restricting TNS to one window removes most of the benefit while
the MOS losses on fatboy, Greensleeves and bas remain, so TNS on FAAC's short windows does not pay with this quantizer:
the item 2a/2b TNS line closes. HE STNS was not scored (stopped by the owner's delivery-mode instruction).
