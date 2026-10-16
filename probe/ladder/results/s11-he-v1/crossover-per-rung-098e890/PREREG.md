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

## Amendment (written after the 49-clip screen, before any held-out scoring)
The screen favoured start 9 and 11 at every rung. Held-out confirmation arms: control (fix15), fix11, fix10, fix9
(10 added as the midpoint; this is an amendment, not a result). The 49 PMLT/SoundExpert references are all the
music available here (PMLT numbered variants are codec-degraded copies of them), so the only independent clips are the
400 TCD-VoIP clean speech references (48 kHz mono). Frozen rule: every 8th file of the C-locale sorted path list
(`heldout_list.txt`, 50 clips), converted with ffmpeg to 48 kHz stereo s16 (dual mono). Limits stated up front: this
held-out set is speech-only and dual-mono, so it tests generalisation to a different signal class, not to unseen music.
Success criterion: BD-rate vs control CI excludes 0 on the favourable side and per-rung mean MOS delta > 0 at >= 3 of 4
rungs. A music held-out claim would need new music clips.

## Amendment 2 (sample-rate generalisation; written before any 44.1/32 kHz crossover scoring)
Corpora: faac-benchmark `audio_44k1` and `audio_32k` (the same 49 clips resampled by setup_datasets.py), HE-AAC v1 forced,
ABR stereo. Rungs keep the per-channel rate of the 48 kHz ladder where valid: 44.1 kHz 40/48/56/64 kbps;
32 kHz 32/40/48/56 kbps (a 64 kbps rung at 32 kHz is 2 bits/sample-ish and outside the HE regime). Arms: control (fix, start 15),
fix11, fix9; bs_start_freq 10 is run on 3 clips only as a decoder-agreement diagnostic. A start value is acceptable at a rate only
if FAAD2 and FFmpeg decodes agree on a probe clip (MOS difference < 0.15); decoder disagreement is reported, not scored over.
Same decision rules as the main screen: BD-rate CI vs control excludes 0 favourably and per-rung deltas positive at >= 3 of 4
rungs; both FAAD2 and FFmpeg decode reported. These clips are not independent of the 49-clip corpus; they test the rate-dependent
kx / master-table path, not generalisation across material. Mono and VBR/CBR remain untested.

## Amendment 3 (written after seeing the 44.1/32 kHz results, before any further 32 kHz scoring)
Observed (FAAD2 and FFmpeg agree): start 9 and 11 lose at 32 kHz (+7.1 % / +4.1 % BD-rate vs control) while helping at 44.1/48 kHz.
The start index is not a rate-invariant quantity: kx = start_min + sbr_offset[row][idx] with different tables per rate, giving
crossover frequencies kx*fs/128:  48 kHz idx 9/11/15 = 6.4/7.5/11.6 kHz;  44.1 kHz idx 9/11/15 = 6.2/7.2/11.0 kHz;
32 kHz idx 9/11/12/13/14/15 = 5.25/5.75/6.25/6.75/7.25/8.0 kHz. Hypothesis (post-hoc, stated as such): the benefit comes from
a crossover near 6-7.5 kHz, so at 32 kHz the Hz-matched indices are 13 (6.75 kHz) and 14 (7.25 kHz). Arms to add at 32 kHz only:
fix13, fix14 (same corpus, rungs, decoders, rules). This is a hypothesis test on already-seen data: any positive result needs the
same kind of independent confirmation and is not a claim. Decoder agreement for 13/14 at 32 kHz was already seen in the scan
(cst/SinceAlways: agree). A single formula for kx as a function of rate_per_ch and sample rate is NOT proposed until this resolves.

## Amendment 4 (new base: nschimme/faac `he-v1-retune` 217f913; written before scoring these arms)
Base moved: 217f913 ("retune HE-AAC v1 for rich cores"): start 12 and light inverse filtering/transient 8 from 20 kbps/ch, stop frequency
capped so k2/kx <= 2.2449 (one-region table). Experiment tree = 217f913 + table fix 51c589e (conflict: comment only) + probe env
`FAAC_PROBE_SBR_START` (rich rungs only) and `FAAC_PROBE_NOCAP` (lift the cap; requires the two-region table). Table fix alone is
payload-identical to 217f913 on 3 clips x 4 rungs (12/12).
Pre-scoring decoder-agreement scan (FAAD2 287c00c, libfaad branch d7fe6f7, FFmpeg 6.1.1, fdkdec; cst/SinceAlways, three rates, starts 9-13):
capped arms agree in all decoders at every start; cap-lifted arms break FAAD2 (MOS 1.3-2.1) at 48k start 10, 44.1k starts 10/11,
32k start 11, while libfaad/FFmpeg/FDK decode them. Therefore: arms are capped starts {13, 11, 10, 9} and cap-lifted starts {12, 9}
(cap-lifted 12/9 decoded correctly everywhere in the scan), control = capped 12 (= base). Corpus/rungs/scorers as in the main screen
(49 clips, 48 kHz, 40/48/56/64, scored with FAAD2, FFmpeg and fdkdec). Decision rules unchanged; the claim "lifting the cap helps" requires
cap-lifted S to beat capped S with CI excluding 0 under the FDK decoder as well. Follow-ups at 44.1 and 32 kHz only for arms that pass at 48 kHz.

## Amendment 5 (44.1 / 32 kHz on he-v1-retune 217f913 + table fix; written before scoring)
Crossover in Hz, kx*Fs/128: 48 kHz idx 9/12/15 = 6.4/8.25/11.6 kHz; 44.1 kHz idx 9/12/15 = 6.2/7.9/11.0 kHz (kx 18/23/32);
32 kHz idx 9/11/12/13/15 = 5.25/5.75/6.25/6.75/8.0 kHz (kx 21/23/25/27/32). The 48 kHz winner (lifted 9) is ~6.4 kHz, so the Hz-matched
indices are 9 at 44.1 kHz and 12-13 at 32 kHz. Arms (same corpora/rungs/decoders as Amendment 2; rich-only override, so the 32 kbps rung at
32 kHz [16 kbps/ch] is unchanged by construction): 44.1 kHz control capped12, lifted12, lifted9, lifted13; 32 kHz control capped12, lifted12,
lifted13, lifted9. Decision: an arm "passes" at a rate if BD-rate vs that rate's control has a CI excluding 0 favourably under FAAD2,
FFmpeg and the FDK decoder. Rungs 44.1 kHz 40/48/56/64, 32 kHz 32/40/48/56. These are resampled copies of the same 49 clips (tests the rate-dependent kx and
table path, not new material).
