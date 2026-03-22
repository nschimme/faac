# Pseudo-SBR Research and MOS Optimization

## Goal
Achieve an average MOS delta lift of **0.3** using pseudo-SBR on `vss`, `voip`, and `music_low` scenarios.

## Experiments

### Iteration 1: Initial Patch (Simple Spectral Copy)
- **Configuration**: `SBR_PATCH_ROLLOFF = 0.5` (-6 dB/octave), `SBR_NOISE_FRAC = 0.09`.
- **Results**:
  - `vss`: Baseline 3.6227 -> Candidate 3.3943 (**Delta: -0.2284**)
  - `voip`: No change (SBR did not activate due to functional bug in integration).
  - `music_low`: No change (SBR did not activate).
- **Observations**: The initial patch caused a significant degradation in MOS for VSS. Integration bug identified: `baseBandWidth` was being recorded as 0 because logic was placed before `CalcBW`.

### Iteration 2: Functional Fix & Enhanced Algorithm
- **Configuration**: `SBR_PATCH_ROLLOFF = 0.707` (-3 dB/octave), `SBR_NOISE_FRAC = 0.09`, **4-bin Cross-fade**, **Comfort Noise Injection (0.005)**.
- **Results**:
  - `vss`: Baseline 3.6227 -> Candidate 3.6954 (**Delta: +0.0727**)
  - `voip`: No change (Delta 0.0000). Nyquist limit constraint at 16kHz.
  - `music_low`: Baseline 3.4409 -> Candidate 3.3471 (**Delta: -0.0938**)
- **Observations**: Achieved a positive lift for `vss`. `music_low` still shows degradation, likely due to bit-starvation of the core spectrum caused by the extra scale factor bands.

## Summary
The integration has been fixed and enhanced with smoothing techniques (cross-fade and comfort noise). A positive lift of **+0.07** was achieved for the `vss` scenario. However, the overall goal of 0.3 lift across all scenarios remains a challenge for future iterations.

## Recommendations for Future Lifts
1. **Bit-Neutral "Strict Stealth" Folding**: Instead of expanding the coded bandwidth with new bands, fill zeroed bins ONLY within the bands already transmitted by the LC encoder. This avoids the side-information overhead that currently starves the core.
2. **Adaptive Slope Matching**: Refine the gain curve to follow the actual spectral tilt of the input signal dynamically.
3. **Harmonic Peak Alignment**: Align SBR patches to core harmonics to reduce metallic/synthetic artifacts.
4. **Perceptual Thresholding**: Only apply SBR when the core bit allocation is sufficient to maintain quality.
