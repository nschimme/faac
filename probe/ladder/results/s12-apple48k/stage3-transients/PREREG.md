# S12 Stage 3 prereg: TNS on short windows (written before any scoring of the arm)

## Causal finding that motivates the arm (decision dumps, `decisions/onsets_lc.md`, `onsets_he.md`, `stereo_shares.md`)
- Over the 697 onsets the benchmark's transient metric scores (77 clips), FAAC is already in EIGHT_SHORT at the aligned
  frame for 94.7 % (LC) / 95.0 % (HE) of onsets, more than Apple LC (84.5 %) and FDK LC (55.5 %). Window choice is not
  where FAAC falls behind (consistent with the closed LC block-switch retunes).
- FAAC never codes TNS on a short window (`libfaac/tns.c` long-only; `frame.c` skips ONLY_SHORT_WINDOW). Apple codes
  TNS on 36 % (LC) / 45-49 % (HE) of short ICS, FDK on 76 % / 68-75 %, at 1.3-1.7 filters of order 2.5-5.4.
- So at an onset the references shape quantization noise in time inside the 256-sample short window (or use long+TNS),
  while FAAC spreads it evenly over the short window, part of which lies in the metric's 6-1 ms pre-onset window.
Hypothesis: TNS on short windows lowers FAAC's excess pre-echo (NPER) where it trails (LC <= 128 kbps, HE-v1 24-48)
without costing MOS.

## Arm (env-gated scratch patch `scripts/s12/stns_scratch.patch`, unset = byte-identical to master)
`FAAC_STNS=1`, defaults fixed now and not tuned on these clips: order 4, prediction-gain bar 1.4 (same as long),
start frequency as the patch documents, LC and HE core. Same binary with the env unset is the control.

## Measurement (48 kHz stereo, 77 clips, FAAD2 / FFmpeg / fdkdec, realized kbps)
- LC ABR 32, 40, 64, 96, 128, 160, 192; LC CBR 96, 128; LC VBR at the benchmark's q for 96/128 (103/160).
- HE-v1 ABR 24, 32, 40, 48.
- Metrics: event-pooled excess pre-echo NPER (benchmark definition: mean of max(0, pre_echo_db) over all eligible
  events of the scenario), post-onset HF residual if the harness exposes it, MOS, stereo fidelity.
- Reading order: (1) worst clip x rung MOS cells vs control (gate -0.10) per decoder; (2) NPER per rung, arm vs
  control vs Apple vs FDK, with a clip-bootstrap 95 % CI of the arm-control NPER difference; (3) BD-rate (MOS) arm vs
  control with CI over LC 32-64, 96-192, HE 24-48.

## Decision
The arm advances to the full-dataset pass (benchmark runner, all scenario families; `PROTOCOL_ADDENDUM.md`) only if:
NPER difference CI wholly below 0 at >= 2 of the targeted rungs (LC 96/128, LC 32/40, HE 24-48) under all three
decoders, no MOS cell below -0.10, and MOS BD-rate CI not wholly above 0 in any range. Any tuning of order, gain bar
or start frequency happens only on the full dataset after that, with its own addendum. Footprint and instruction
deltas are reported; growth of `.text+.rodata+.data` is flagged for an owner decision rather than hidden.
