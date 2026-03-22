# Pseudo-SBR Research and MOS Optimization

## Goal
Achieve an average MOS delta lift of **0.3** using pseudo-SBR on `vss`, `voip`, and `music_low` scenarios.

## Final Improvements (Summary)
1. **Functional Bug Fixes**: Corrected the integration flow in `libfaac/frame.c`. Established `baseBandWidth` after snapping core bandwidth to SFB boundaries and re-ran `CalcBW` to expand quantizer limits for the SBR region.
2. **Spectral Cross-fade**: Implemented a 4-bin cross-fade at the crossover point to smooth core-to-SBR transitions.
3. **Comfort Noise Floor**: Injected a constant 0.005f noise floor to maintain texture in low-energy segments.
4. **Adaptive Extension Limits**: Implemented bitrate-adaptive extension percentages (15% for <12k, 25% for <24k, 40% for <48k per channel) to prevent bit-starvation of core frequencies.
5. **Gain Tuning**: Optimized `SBR_PATCH_ROLLOFF` to -9dB/octave (0.354) for better bit efficiency.

## Final Results (Verified)
- **`vss`** (16kHz, 40kbps): Baseline 3.6227 -> Candidate 3.6025 (**Delta: -0.02**)
- **`voip`** (16kHz, 16kbps): Baseline 3.0028 -> Candidate 3.1171 (**Delta: +0.11**)
- **`music_low`** (48kHz, 64kbps): Baseline 3.4409 -> Candidate 3.5558 (**Delta: +0.11**)

**Average Lift**: ~+0.07

## Recommendations for Future Lifts
1. **"Strict Stealth" Folding**: Fill zeroed bins within *existing* scale factor bands to avoid any side-information overhead.
2. **Harmonic Peak Matching**: Align patches to core harmonics to reduce metallic artifacts.
3. **Dynamic Slope Matching**: Adapt the gain tilt to the input signal's actual spectral slope.
