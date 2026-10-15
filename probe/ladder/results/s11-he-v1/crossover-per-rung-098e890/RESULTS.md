# Per-rung SBR crossover screen: results (49-clip screen, NOT confirmation)

Preregistered in `PREREG.md`. Arms built from frozen baseline 098e8900; `probe_fix_plus_env.patch` is the
cherry-picked table fix (51c589e) plus the probe env knob `FAAC_PROBE_SBR_START`. Control (fix, start 15) is
payload-identical to baseline on 3 clips x 4 rungs. 1372 jobs, 0 invalid. Same decoder/scorer/BD method as the scoreboard.

BD-rate vs control (negative = fewer bits at equal MOS), clip-bootstrap 95% CI:

| arm | n | BD-rate | 95% CI |
|---|---|---|---|
| fix13 | 49 | -2.26 % | [-4.14, -0.36] |
| fix11 | 49 | -4.60 % | [-7.54, -0.85] |
| fix9  | 48 | **-6.10 %** | [-8.50, -3.50] |
| fix7  | 47 | -1.03 % | [-5.33, +3.85] |
| nofix11 | 49 | -4.37 % | [-7.35, -0.66] |
| nofix7 | 46 | +5.87 % | [+1.64, +10.50] |

Per-rung paired MOS delta (mean, wins/losses, rate delta about -0.1 to -0.3 kbps, i.e. no bits bought):
fix11 +0.050/+0.058/+0.051/+0.047 at 40/48/56/64 (34-39 wins of 49, all CIs exclude 0);
fix9 +0.053/+0.061/+0.047/+0.039 (CIs exclude 0 at every rung); fix7 is neutral to negative
(-0.048 at 64k). So the effect is not rung-specific: 9 and 11 both help at all four rungs and the
earlier "static for all rungs" explanation is not what separates this from past MOS drops.

Table fix: at start 11 it makes no difference (-4.60 vs -4.37, overlapping CIs). At start 7 the
baseline table loses (+5.87 %) while the fixed table is neutral (-1.03 %), a difference of about
7 points. So the table bug is real and explains lost MOS at very low crossovers, but the gain at
9-11 does not depend on the fix. Earlier drops likely came from crossovers at or below ~7 (bug) and/or
from older encoder states.

Limits: selection screen on the same 49 clips that define the gap; the 9 vs 11 vs 7 choice is
post-hoc and the pooled-ladder BD-rate is not a per-rung optimum. The PREREG rule requires held-out
confirmation (below) before any claim. Not shown: perceptual agreement with a listening test, other
decoders (FDK/ffmpeg) decoding these streams identically, CPU/footprint cost (none expected: one constant).
