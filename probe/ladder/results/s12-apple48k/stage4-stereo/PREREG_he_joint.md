# S12 Stage 4 prereg: HE-v1 stereo mode screen at 32/40/48 (CLI only, written before scoring)

## Evidence (stage 1, no arm scored yet)
- HE-v1 stereo fidelity (FAAD2) FAAC 0.917 / 0.958 / 0.977 at 32 / 40 / 48k vs FDK 0.973 / 0.982 / 0.985, Apple
  0.968 / 0.973 / 0.977. FAAC leads mean MOS at all three rungs and trails FDK/xaac P1 at 40/48k.
- Bottom-five P1 clips at 40/48k are mostly the stereo-fidelity losers (07 applaud, 18vr_, 13 fatboy; `stage1-rebaseline/he/losers.md`).
- Master's tiers (`stereo.c`, per-channel rate r): 32k = 16 kbps/ch is starved below `MS_SPLIT_HE_LOW_BITRATE` (20k):
  no M/S, IS on long and short windows (dump: M/S 0 %, IS 46 % of CPE bands). 40k = 20k/ch: M/S below 1 kHz only.
  48k = 24k/ch (`MS_SPLIT_HE_HIGH_BITRATE`): long windows all-M/S, short below 2 kHz. FDK codes 86 % of HE CPE bands
  M/S with 2-3 % IS; Apple 71-74 % M/S, 15-18 % IS. These gates were never measured on this corpus.

## Screen
- Arms: default (auto), `--joint ms`, `--joint lr`, `--joint is`. HE-v1 CBR 32 / 40 / 48 kbps, 77 clips,
  FAAD2/FFmpeg/fdkdec, realized kbps. Default rows = `stage1-rebaseline/rows_he_cbr.jsonl`.
- Reading order: worst clip x rung MOS vs default (gate -0.10) per decoder; stereo fidelity per rung (mean, and
  clip-bootstrap CI of the arm-default difference); P1 and mean MOS per rung; BD-rate vs default (PCHIP and linear,
  sign counts) over 32-48; NPER per rung.
- A mode "clears" at a rung if its stereo-fidelity difference CI is wholly above 0 under all three decoders, no cell
  falls below -0.10, mean MOS does not drop by more than 0.005, and P1 does not drop. A clearing mode leads to an
  env-gated scratch patch of the matching tier gate (rate threshold or M/S split frequency) with its own prereg; the
  gate value is then picked on the full dataset (PROTOCOL_ADDENDUM). If no mode clears, the HE stereo-tier lead
  closes as negative for CLI modes and the next hypothesis is the IS/M-S decision itself.
