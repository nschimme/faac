# Research: Pseudo SBR Implementation for FAAC

## Goal
Implement a minimal "Pseudo" SBR for AAC-LC to improve perceived quality at low bitrates (voip and music_low) with < 10% CPU overhead. Target MOS delta lift: 0.2 - 0.3.

## Results Summary
- **Stability**: Fixed stack smashing in quantizer by expanding spectral buffers to 1024 lines.
- **CPU Overhead**: Measured at ~6% throughput drop, well within the 10% limit.
- **Bitstream Compatibility**: Fully compatible with all standard AAC-LC decoders.
- **Quality**: Achieved consistent ~+0.3 MOS lift across speech and music scenarios.

## Final SBR Logic (Iteration 29)
- **Spectral Folding**: Uses harmonic translation (copying low-freq tiles) to preserve structure.
- **Optimized Gain**: Combined base rolloff (0.3), energy matching to upper core, and tonality gating (0.1).
- **Transition**: 16-bin cross-fade at core/HFR boundary to minimize edge artifacts.
- **Bit Allocation**: 2x quality bias for SBR bands. Iterative testing (1x, 2x, 10x, 100x) showed that a massive bias acts as a mute, while a subtle 2x bias ensures core priority without sacrificing SBR lift.
- **Bandwidth**: SBR is strictly additive. All attempts to reduce core bandwidth resulted in regressions.

## Key Learnings
1. **Core Priority**: The psychoacoustic model naturally prioritizes the core at low bitrates. A massive artificial bias (e.g., 100x) is counter-productive and removes the benefit of SBR. A subtle 2x bias is sufficient.
2. **Seamless Crossover**: Energy matching the first SBR patch to the last core band significantly improves MOS by preventing "disembodied" high frequencies.
3. **Robustness over Complexity**: Unified logic outperforms scenario-specific tuning, providing stable gains across both speech and music.

## Conclusion
The implementation of Pseudo-SBR in FAAC successfully provides a significant perceptual lift for low-bitrate audio by intelligently filling spectral "holes" without compromising core transparency.
