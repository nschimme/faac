# S12 Stage 3 prereg: short-window TNS v2, attack windows only (written before the v2 arm is scored)

Evidence: v1 (`RESULTS_stns_v1_lc.md`) cut pooled NPER by 1.0-3.8 dB at LC 32-128 (all CIs below 0, three decoders) but
failed the MOS gate (worst 13 fatboy -0.59; bas.wav, Greensleeves speech, 14fe_ -0.14..-0.22), with losses on clips that
are mostly long-window too, i.e. from TNS on short windows that hold no attack.

Arm `stns2` (`FAAC_STNS=1 FAAC_STNS_ATK=1`): same filter design and defaults as v1 (order 4, gain bar 1.4, 1.5 kHz), but
within an EIGHT_SHORT frame only the attack window gets TNS: the short window with the largest energy rise over its
predecessor (window energy from the MDCT spectrum, ratio > 1, i.e. no extra threshold), and none if no window rises.
Rule fixed now; no tuning here.

Measurement and decision as in `PREREG.md` (LC ABR 32/40/64/96/128, HE-v1 CBR 24/32/40/48; worst cells first; NPER
difference CI; MOS, P1, BD-rate both methods). Advance only if: NPER CI below 0 at >= 2 targeted rungs under all three
decoders, no cell below -0.10, mean MOS not lower by more than 0.005 at any rung, P1 not lower. Instruction and size
deltas reported.
