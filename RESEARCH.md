# Pseudo-SBR Research and MOS Optimization

## Goal
Achieve an average MOS delta lift of **0.3** using pseudo-SBR on `vss`, `voip`, and `music_low` scenarios.

## Final Improvements (Summary)
1. **Adaptive Quantization Penalty**: Implemented a bitrate-dependent `sfac` penalty (+4 to +12) in `libfaac/quantize.c` for SBR bands. This ensures that synthetic HF content doesn't starve the core spectrum of bits, which was the primary cause of early regressions.
2. **Adaptive Gain Tilt**: Refined the rolloff curve in `libfaac/pseudo_sbr.c` to be bitrate-aware (-3dB to -10dB). Higher bitrates use steeper rolloff to remain "stealthy" and minimize artifacts.
3. **Adaptive Noise Fraction**: Dynamically adjusted the noise injection (0.05 to 0.30) based on bitrate.
4. **Grouping Bugfix**: Corrected a bug in `libfaac/stereo.c` where joint-stereo processing assumed the first channel's group count for all channels.
5. **Critical Call Site Move**: Moving `PseudoSBR` to before TNS/Stereo in `libfaac/frame.c` allows the core encoder to optimize the folded content.
6. **Perceptual Texture**: Added spectral mirroring (sign-flipping) and 4-bin cross-fading to reduce "grittiness" and smooth transitions.

## Final Results (Verified)
- **`vss`** (16kHz, 40kbps): Baseline 3.6227 -> Candidate 3.7243 (**Delta: +0.10**)
- **`voip`** (16kHz, 16kbps): Baseline 3.0028 -> Candidate 3.1057 (**Delta: +0.10**)
- **`music_low`** (48kHz, 64kbps): Baseline 3.4409 -> Candidate 3.5558 (**Delta: +0.11**)

**Average Lift**: **+0.10**

## Analysis
The combination of adaptive quantization and steeper gain tilt at higher bitrates successfully eliminated the regressions seen in the `vss` and `music_low` scenarios. While the +0.3 average lift goal remains ambitious for a purely encoder-side LC spectral extension, the current implementation provides a robust and consistent quality improvement across all tested bitrate tiers.

## Recommendations for Future Lifts
1. **"Strict Stealth" Folding**: Investigate filling ONLY zeroed core bins within existing scale factor bands to achieve true "bit-neutral" extension.
2. **Harmonic Peak Matching**: Further refine the harmonic alignment shift to handle complex polyphonic audio.
3. **Signal-Adaptive rolloff**: Calculate the target rolloff based on the real-time spectral tilt of the unquantized input.
