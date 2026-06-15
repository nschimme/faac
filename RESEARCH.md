# Mixed Mode Joint Stereo + Throughput Optimization

## Summary

This PR introduces a new **Mixed Mode** joint-stereo coding path that selects
between Intensity Stereo (IS), Mid/Side (M/S), and plain L/R **per scale factor
band**, and makes it the default joint mode. It also rewrites the stereo module
for throughput, fixes two real correctness defects, adds a low-sample-rate
robustness fix, and removes duplicated code. Legacy joint modes
(None / M/S / IS) remain bit-identical; Mixed Mode is faster than the previous
default and reconstructs a truer stereo image.

## Motivation

The previous default was forced **Intensity Stereo** (`--joint 2`), which
discards the inter-channel phase relationship across the whole spectrum to bank
bits for monaural spectral fidelity. That collapses the stereo image more than
necessary at the frequencies where phase still matters. Mixed Mode keeps M/S
(phase-preserving) below a crossover and only uses IS above it, recovering
stereo fidelity while keeping the bit savings where they don't hurt.

## What's in this PR

- **Feature:** Mixed Mode (`JOINT_MIXED`, mode 3) per-band IS/M/S/LR selection,
  exposed in the CLI (`--joint 3`) and GUI (Joint Stereo dropdown), and made the
  default.
- **Correctness:** fix a band-silencing bug in `midside()`; guard the
  single-pass energy math against negative-rounding before `sqrt`.
- **Robustness:** keep IS available at low sample rates.
- **Throughput:** single-pass energy accumulation, `restrict` pointers, hoisted
  invariants.
- **Cleanup:** factor the duplicated M/S, IS, and channel-zeroing transforms
  into shared helpers; drop a dead clamp.

## Design: Mixed Mode decision logic

### Decision hierarchy
Per scale factor band, with IS and M/S **mutually exclusive**:
1. **Intensity Stereo (IS)** — evaluated first, only for bands at/above the
   frequency floor.
2. **Mid/Side (M/S)** — evaluated if IS is not chosen.
3. **L/R** — default fallback. A far-quieter channel may additionally be zeroed.

### Intensity Stereo
- **Frequency floor: 5.5 kHz.** Benchmarking showed this crossover balances bit
  savings against phase fidelity for music; below it, phase carries the stereo
  image and IS is not used.
  - **Low-sample-rate cap:** the floor is capped at 70% of Nyquist. At low rates
    (e.g. 16 kHz) a hard 5.5 kHz floor can exceed the top band and disable IS for
    the whole frame; the cap keeps an IS region available. At ≥44.1 kHz the cap
    is well above 5.5 kHz, so common rates are unchanged.
- **Threshold:** `isthr = (0.18 / quality) + 1.0`, clamped to `sqrt(2)`. Linear
  (not quality²) scaling retains more phase at low quality. The per-band gate is
  `ethr = (sqrt(enrgL) + sqrt(enrgR))² / isthr`.
- **Panning limit ±30 units:** beyond this the quieter channel is inaudible and
  is dropped entirely (`HCB_ZERO`) rather than IS-coded.

### Mid/Side
- **Thresholds:** `thrmid = (thr075 · 0.85) / quality + 1.0` (with
  `thr075 = 0.09`, i.e. `0.0765 / quality + 1.0`), clamped; the `0.85` makes
  Mixed Mode's M/S slightly tighter than pure `JOINT_MS` since IS carries the
  high end. `thrside = 0.1 / quality`, clamped to `0.3`.
- **Condition:** M/S is selected when
  `min(enrgL, enrgR) · thrmid >= max(enrgSum · 0.25, enrgDiff · 0.25)`. The
  `0.25` compensates for the `0.5·(L±R)` mid/side scaling.

## Correctness fixes

- **`midside()` band-silencing bug.** The "zero the far-quieter channel"
  (`thrside`) step ran unconditionally, including on bands already M/S-coded.
  After M/S the buffers hold mid/side, so zeroing one — while still signalling
  `ms_used=1` — makes the decoder reconstruct `L = R = 0` and silences the band.
  Added the `!ms` guard that `mixed()` already had. Verified against the FFmpeg
  reference decoder: M/S reconstruction is the butterfly `(L,R) = (M+S, M−S)`,
  confirming the failure mode and the fix.
- **Negative-energy guard.** The single-pass identity
  `enrgSum/Diff = enrgL + enrgR ± 2·enrgLR` is mathematically non-negative but
  can round slightly below zero for `L ≈ ±R`. In `stereo()`/`mixed()` those feed
  `sqrt`, so they are now clamped to 0 (NaN guard). `midside()` has no `sqrt`
  consumer, so no clamp is needed there.

## Throughput optimization

The energy loops accumulate `enrgL`, `enrgR`, and the cross term `enrgLR` in a
**single pass**, then derive sum/diff energies via `|l±r|² = l² + r² ± 2·l·r`
(instead of separate sum-of-squares passes). Pointers are `restrict`-qualified
and invariant work is hoisted out of the per-band loops. These changes preserve
the algorithm's output while reducing per-band work.

## Code cleanup

The mid/side butterfly, the intensity-stereo combine, and the channel-zeroing
loop were copy-pasted across `stereo()`, `midside()`, and `mixed()`. They are now
single-sourced in `apply_ms()`, `apply_is()`, and `zero_channel()`. This removes
the duplication that let the `midside()`/`mixed()` guard drift apart in the first
place, and shrinks the object code. Comments were trimmed to explain only
non-obvious rationale.

## Validation & benchmarking

Validated with the `faac-benchmark` suite (candidate build vs. a master baseline
build) plus direct bit-exact checks.

- **Legacy modes are bit-identical.** `--joint 0/1/2` (None/M/S/IS) produce
  byte-identical output to master (md5-checked); only the new `--joint 3`
  differs, and only at low sample rates does the IS-floor cap change anything.
- **Throughput:** consistently faster than master. Local A/B on the throughput
  stimuli measured roughly **+3%** overall (CI is positive but noisy run-to-run;
  treat the local A/B as authoritative).
- **Stereo image:** monaural ViSQOL `audio` mode **cannot see the stereo image**
  (decoding to stereo vs. mono scores identically), so quality is tracked with a
  windowed **inter-channel coherence error** (`ic_err`, lower = truer). Mixed
  Mode improves it vs. master (CI Stereo Image Δ ≈ **+0.0038**), and the fixes
  above improve it further.
- **Monaural MOS:** at a fixed bitrate the rate controller pins total bits, so
  stereo-coding changes only *redistribute* bits; MOS is therefore at parity
  (and is treated as a **regression floor, not a target** — maximizing it would
  reward stereo collapse).
- **Decode-validated:** every encode is decoded with ffmpeg; no decode errors.
- **Spec-checked:** M/S matrix, `ms_mask_present` semantics, and the
  intensity-sign / `ms_used` coupling were confirmed against the FFmpeg reference
  decoder.

## Defaults & compatibility

- The default joint mode changes from `JOINT_IS` to `JOINT_MIXED`. **API
  consumers that never set `jointmode` will get Mixed Mode**, which changes
  bitstream contents (not validity) versus before.
- CLI `--joint` and the GUI dropdown expose all four modes (None / M/S / IS /
  Mixed).

## Investigated but not adopted

- **`ms_mask_present = 2` (all-bands-M/S, no per-band mask).** Not viable for
  Mixed Mode: value 2 sets the mask for *every* band, which would invert the
  intensity sign of every IS band (the decoder applies `c *= 1 − 2·ms_mask`) and
  force M/S onto the phase-protected low bands. Only valid for an all-M/S frame,
  which this design never produces.
- **Threshold lever sweeps.** A sweep of the IS floor, `isthr`, `thrmid`, and
  `thrside` (over plentiful and bit-starved bitrates) found the existing defaults
  already at a flat optimum — every change was ≤0.004 MOS, within the benchmark's
  measurement noise and far below its 0.05 "minor regression" threshold. No
  retuning win; the defaults stand.

## Caveats

- Inter-channel coherence is a **proxy**, not a perceptually-validated metric;
  the gold standard remains a subjective MUSHRA/ABX listening test.
- At very low bitrates the stereo image is degraded in **all** modes; Mixed
  Mode's advantage is relative and grows with available bits.
