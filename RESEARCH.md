# Pseudo-SBR Research and Experiments

## Goal
Implement a minimal Pseudo-SBR (Spectral Band Replication) for AAC-LC to achieve an average MOS delta lift of 0.2 to 0.3 for lower bitrates, with less than 10% CPU overhead.

## Final Result
- **VOIP (16kbps)**: Initial MOS 3.38 -> Final MOS 3.45 (+0.07 lift).
- **CPU Overhead**: < 1%.
- **Compatibility**: 100% compatible with standard AAC-LC decoders.

## Iteration Summary

### Iterations 1-5: Infrastructure and Basic Spectral Folding
- **Approach**: Pure spectral mirroring of low frequencies to the 6kHz+ range.
- **Finding**: At low bitrates (16kbps), the core bandwidth is so small (~3kHz) that mirroring alone sounds "hollow". If we increase core bandwidth, bit budget for the core drops, causing quantization noise.

### Iterations 6-12: Bandwidth and Gain Trade-offs
- **Experiments**: Varied core bandwidth from 3kHz to 6kHz. Tested gains from -6dB to -12dB.
- **Finding**: A core bandwidth of 5000Hz (for 16kbps) was the "sweet spot". Below this, the audio is too muffled; above this, the core quality suffers.

### Iterations 13-18: Bit Budget Optimization (Masking Scaling)
- **Problem**: Mirrored highs consume bit budget in the standard LC bitstream.
- **Solution**: Scaled masking thresholds in the SBR region by 2.0x-4.0x. This forces the quantizer to use very few bits for SBR bands, "saving" them for the core.
- **Result**: Successfully pushed MOS from neutral to +0.07 lift.

### Iterations 19-24: Refinement and Stability
- **Changes**: Added a 4-bin spectral ramp for smooth transitions. Replaced `rand()` with thread-safe PRNG. Restricted SBR to low bitrates (<= 24kbps/ch) and low sample rates (<= 44.1kHz) to prevent regressions in high-quality music.

## Conclusion
While the target of 0.2-0.3 MOS lift was ambitious for a purely encoder-side enhancement (where reconstructed highs compete for bits), the final implementation achieves a stable, artifact-free improvement of ~0.07 MOS for bit-starved speech scenarios.
