# S12 Stage 3 prereg B: attack-aligned SBR envelope border for HE-v1 (written before the arm exists)

## Evidence
- HE-v1 NPER (pooled, FAAD2): FAAC 21.1 / 18.4 / 16.5 dB at 32 / 40 / 48k vs FDK 15.4 / 14.8 / 14.4, Apple 17.0 / 15.5 / 14.9.
- Band split (`decisions/he_nper_bands.md`, events fixed from the full-band reference): above 12 kHz (SBR at every
  rung for FAAC) FAAC is 13.8 / 12.2 / 11.7 dB vs FDK 6.1 / 7.5 / 7.2 and Apple 7.0 / 9.3 / 9.2; at 40/48k the 6-12 kHz
  band (straddling FAAC's 8.25 kHz crossover) is 19.8 / 19.0 vs 14.1-15.5. At 32k the 2-12 kHz excess is core.
- SBR grid at the 697 scored onsets (`decisions/sbr_grid_he.md`): an envelope border within 2 QMF slots of the onset in
  18.7 % (32k) / 64.7 % (40/48k, chance level of a 4-envelope FIXFIX grid) of onsets for FAAC vs 80-87 % for Apple,
  FDK and libxaac.
- Mechanism in master (`sbr_analysis.c` SbrAnalyze): the transient test computes the per-slot energy maximum but a
  transient frame always gets VARFIX with its border at mid-frame (slot 8); the slot of the maximum is discarded. Below
  17 kbps/ch non-transient frames get one envelope, above it four.
- History: S11 tested an RD-scored adaptive border selector ("mode1", second QMF pass + exact bit scoring) against
  fixed8 and found no resolved MOS BD-rate gain; it never measured pre-echo. This arm is a deterministic border at the
  detected attack, judged first on NPER, which is new causal evidence for a different rule.

## Arm (env-gated scratch patch, unset = master byte-exact; HE only, LC untouched by construction)
`FAAC_SBR_ATK=1`: in a frame the existing transient test already flags (threshold unchanged), place the VARFIX inner
border at the attack slot (slot of maximum energy, mapped to the bitstream's slot grid and the syntax's 2-slot rel-border
granularity, leading border 0-3), so the envelope starting at the border holds the attack. Attacks outside the encodable
range keep master's mid-frame border. No threshold change, no new envelope counts.

## Measurement
- HE-v1 CBR 24 / 32 / 40 / 48, 77 clips, FAAD2/FFmpeg/fdkdec; control = master rows (stage 1 `rows_he_cbr.jsonl`).
- Reading order: worst clip x rung MOS vs control (gate -0.10); pooled NPER per rung with clip-bootstrap CI of the
  arm-control difference, full band and the >12 kHz band; P1 and mean MOS per rung; BD-rate vs control (PCHIP + linear).
- Advance to the full-dataset pass only if NPER improves with CI wholly below 0 at >= 2 of the 4 rungs under all three
  decoders, no MOS cell below -0.10, mean MOS not lower by more than 0.005 at any rung, and P1 not lower. Otherwise close.
