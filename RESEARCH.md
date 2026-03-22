# Pseudo-SBR Research and MOS Optimization

## Goal
Achieve an average MOS delta lift of **0.3** using pseudo-SBR on `vss`, `voip`, and `music_low` scenarios.

## Final Improvements (Summary)
1. **Critical Call Site Move**: Moved the `PseudoSBR` call to **before** TNS and Stereo processing in `libfaac/frame.c`. This allows the encoder's core tools to optimize the folded content, which was the single most important factor in achieving positive MOS lifts.
2. **Spectral Mirroring**: Implemented sign-flipping (`i % 2 ? -src : src`) in `libfaac/pseudo_sbr.c` to decorrelate patches and reduce the "gritty" synthetic texture.
3. **Harmonic Alignment**: Implemented a peak-matching shift in source selection to align folded harmonics with the core signal, reducing metallic artifacts.
4. **Spectral Cross-fade**: Implemented a 4-bin cross-fade at the crossover point to smooth the transition.
5. **Conservative Comfort Noise**: Injected a very low level (0.0002f) of constant noise floor. This maintains texture without breaking Huffman zero-runs or significantly increasing bit-cost.
6. **Bitrate-Adaptive Extension**: Implemented percentages (15-40%) based on bitrate to protect core quality.
7. **Optimized Gain**: Tuned `SBR_PATCH_ROLLOFF` to -8dB/octave (0.398).

## Final Results (Verified)
- **`vss`** (16kHz, 40kbps): Baseline 3.6227 -> Candidate 3.7243 (**Delta: +0.10**)
- **`voip`** (16kHz, 16kbps): Baseline 3.0028 -> Candidate 3.1135 (**Delta: +0.11**)
- **`music_low`** (48kHz, 64kbps): Baseline 3.4409 -> Candidate 3.5558 (**Delta: +0.11**)

**Average Lift**: **+0.11**

## Analysis
While the target of 0.3 average lift was not reached, the implementation now consistently provides quality improvements across all tested scenarios. The move to pre-stereo/TNS processing transformed the feature from a quality-negative bit-waster into a beneficial spectral extension tool.

## Recommendations for Future Lifts
1. **"Strict Stealth" Folding**: Explore filling only zeroed bins within the core transmitted bands to reach "bit-neutral" extension.
2. **Adaptive Slope Matching**: Dynamically calculate the roll-off based on the core's spectral tilt.
3. **Full HE-AAC SBR Integration**: If bitstreams are allowed to be non-LC compatible, implementing standard SBR would provide much higher lifts.
