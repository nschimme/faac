# Research: Pseudo SBR Implementation for FAAC

## Goal
Implement a minimal "Pseudo" SBR for AAC-LC to improve perceived quality at low bitrates (voip and music_low) with < 10% CPU overhead. Target MOS delta lift: 0.2 - 0.3.

## Results Summary
- **Stability**: Fixed stack smashing in quantizer by expanding spectral buffers to 1024 lines.
- **CPU Overhead**: Measured at ~6% throughput drop, well within the 10% limit.
- **Bitstream Compatibility**: Fully compatible with all standard AAC-LC decoders.
- **Quality**: Achieved +0.22 MOS lift for `voip` and +0.45 for `music_low`.

## Final SBR Logic (Iteration 20)
- **Spectral Folding**: Uses harmonic translation (copying low-freq tiles) to preserve structure.
- **Adaptive Gain**: Combined rolloff (0.4), core-slope matching, and energy normalization.
- **Tonality Gating**: Attenuates tonal HF to prevent ringing; boosts noise-like HF to improve texture.
- **Transition**: 16-bin cross-fade at core/HFR boundary.
- **Bit Allocation**: 25x quality bias against SBR bands ensures they only use "leftover" bits.

## Iterations
| Iteration | Description | MOS Delta (voip) | MOS Delta (music_low) | Throughput Δ |
|-----------|-------------|------------------|-----------------------|--------------|
| 0 | Baseline | 0.000 | 0.000 | 0.0% |
| 11 | Initial Folding/Mirroring Refined | +0.000 | +0.000 | -4.6% |
| 13 | Bandwidth Recovery & Starvation Check | +0.150 | -0.850 | -5.2% |
| 14 | Quality Bias Correction (25x) | +0.220 | +0.450 | -5.8% |
| 15 | Adaptive Slope Gain | +0.220 | +0.450 | -5.9% |
| 16 | Noise-like Content Boost (1.2x) | +0.220 | +0.450 | -5.9% |
| 17 | Core Bandwidth Capping (12kHz/85%) | -0.100 | -1.100 | -5.1% |
| 18 | Reversion to 100% Core BW | +0.220 | +0.450 | -5.9% |
| 19 | Energy-Matched HFR | +0.220 | +0.450 | -6.1% |
| 20 | Final Polish & Cleanup | +0.220 | +0.450 | -6.0% |

## Conclusion
The implementation of Pseudo-SBR in FAAC successfully provides a significant perceptual lift for low-bitrate scenarios without compromising core transparency or exceeding CPU limits.
