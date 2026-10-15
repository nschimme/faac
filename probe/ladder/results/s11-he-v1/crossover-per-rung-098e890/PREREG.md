# Preregistration: per-rung SBR crossover (bs_start_freq) on frozen baseline 098e8900

Written before any crossover MOS was scored. Probe only; never merge.

## Question
Earlier crossover tests were static (one value for all rungs) and lost MOS. Two confounds:
(1) at bs_start_freq < 15 FAAC's master table can disagree with the decoder's (branch
`sbr-two-region-master-table-16452427657267740547`, commit 51c589e: every envelope lands in the wrong band
once k2/kx > 2.2449), so a lower crossover may have been penalised by a table bug, not by the crossover;
(2) a fixed value cannot show a rate-dependent optimum. Does a lower crossover, with the table fixed and chosen
per rung, improve HE 48 kHz stereo at 40/48/56/64 kbps? The core band follows the crossover automatically
(`SbrContextGetXOverBandwidth`).

## Arms (all from frozen baseline 098e8900; sources/decoder/scorer as in `scoreboard-refresh-098e890`)
- control: baseline + table fix, bs_start_freq 15 (verified payload-identical to baseline on 3 clips x 4 rungs).
- fix-S for S in {13, 11, 9, 7}: baseline + table fix + probe env `FAAC_PROBE_SBR_START=S`.
- nofix-S for S in {11, 7}: baseline + probe env only (table as in baseline). This isolates the table bug.
Rungs 40/48/56/64 ABR, 49 clips, same FAAD2 decode, zimtohrli scorer, actual packet bitrate.

## Metrics and decision rules
- Primary: per-clip PCHIP BD-rate of each arm vs control over the 4-rung ladder (negative = better), clip-bootstrap 95% CI.
- Secondary (per rung, since the ladder pools rungs): paired mean MOS delta and wins/losses at each rung,
  with the rate delta charged (compare at the actual bitrate; report both).
- A per-rung selection (best S per rung) is a post-hoc pick from this screen: it is NOT evidence. If any arm shows a
  CI excluding 0 on the favourable side at some rung, it advances to a confirmation on a held-out clip set chosen and
  frozen before looking (the 49 clips are the confirmation corpus used for the selection, not independent).
- Table-bug test: if fix-S beats nofix-S at the same S with a CI excluding 0, the confound is real.
- Stop: if no arm improves on control at any rung (CI includes 0 or unfavourable) the lever stays closed and the
  statement "lower crossover loses" is recorded as holding per rung with the table fixed.
- Not in scope: stop frequency, PNS, thresholds, loudness/PEAKW (other agents), decoder differences.

## Known limits
- bs_start_freq below 15 moves the core/SBR split and the spectrum SBR must synthesise; envelope-bit cost changes the
  core budget through rate control (charged via actual bitrate).
- Only FAAD2 is used to decode; table correctness is also checked implicitly by MOS, not by an independent decoder.
