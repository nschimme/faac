# S12 item 1 prereg: bandwidth cutoff at LC 192-320 (CLI screen, written before scoring)

## Evidence (`groups.md`, decision dumps, no MOS)
At 192/256/320 FAAC codes to about 18.6 kHz on every clip (`BANDWIDTH_CEILING` 18750 in frame.c), Apple to 20.1 kHz
at 192 and to 22-24 kHz at 256/320. Over the 76 clips the per-clip linear BD vs Apple (192-320) rises with FAAC's
extra zero share in 16-20 kHz (Spearman +0.38) and with source energy above 12 kHz (+0.23). Moderate, not causal.
Cutoff is not on the closed list ("-c 20000" at 128-192 was neutral on the old set, CI spanning 0; it was never
measured at 256/320).

## Screen (existing switch only)
- Arms: default (master), `-c 20000`, `-c 22000`. LC 48 kHz stereo ABR 192/256/320, 77 clips, FAAD2/FFmpeg/fdkdec,
  realized kbps. Default rows come from stage 1 (`rows_lc_abr.jsonl`, same binary and flags).
- Reading order: worst clip x rung vs default (gate -0.10) per decoder; BD-rate vs default and vs Apple over 192-320
  with both methods (PCHIP and linear) and per-clip sign counts; per-rung mean MOS, kbps, NPER, P1.
- Advance: an arm advances to a CBR check and then the full-dataset pass only if both BD methods give a CI wholly below
  0 vs default under all three decoders, with no cell below -0.10. The cutoff value for any PR is picked on the full
  dataset (PROTOCOL_ADDENDUM), not here. Otherwise item 1's cutoff lead closes as negative at 192-320.
