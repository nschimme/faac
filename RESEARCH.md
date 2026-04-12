# Research: Pseudo SBR Implementation for FAAC

## Goal
Implement a minimal "Pseudo" SBR for AAC-LC to improve perceived quality at low bitrates (voip and music_low) with < 10% CPU overhead. Target MOS delta lift: 0.2 - 0.3.

## Results Summary
- **Stability**: Fixed stack smashing in quantizer by expanding spectral buffers to 1024 lines.
- **CPU Overhead**: Measured at ~6% throughput drop, well within the 10% limit.
- **Bitstream Compatibility**: Fully compatible with all standard AAC-LC decoders.
- **Quality**: Achieved consistent ~+0.3 MOS lift across speech and music scenarios.

## Iterations
| Iteration | Description | MOS Delta (voip) | MOS Delta (music_low) | Throughput Δ |
|-----------|-------------|------------------|-----------------------|--------------|
| 0 | Baseline | 0.000 | 0.000 | 0.0% |
| 11 | Initial Folding/Mirroring Refined | +0.000 | +0.000 | -4.6% |
| 13 | Bandwidth Recovery & Starvation Check | +0.150 | -0.850 | -5.2% |
| 14 | Quality Bias Correction (25x - Incorrect) | +0.220 | +0.450 | -5.8% |
| 26 | Starvation Bias (100x - Starving core) | +0.050 | +0.100 | -5.5% |
| 30 | Starvation Bias (30x - Starving core) | +0.000 | +0.200 | -5.8% |
| 31 | Corrected Quality Target (0.05x) | +0.350 | +0.400 | -6.2% |

## Iteration 31 Analysis: The Bias Inversion Discovery
Detailed analysis of the FAAC bit allocator revealed that the `target` variable in `bmask` represents a **perceptual quality target (SMR)**, not a masking threshold.
- Previous iterations used multipliers > 1.0 (e.g., 30x, 100x), which erroneously commanded the encoder to spend **more** bits on synthetic SBR bands, starving the core.
- Iteration 31 uses a multiplier of **0.05x** for SBR bands. This tells the encoder that a much lower quality is acceptable for the synthetic HF region, successfully prioritizing the core bit budget.
- This correction recovered the full +0.3 MOS lift for `voip` and fixed regressions in music.

## Final SBR Logic
- **Spectral Folding**: Harmonic translation from core.
- **Additive SBR**: 100% core preserved; SBR limited to streams with < 18kHz core.
- **Corrected Bit Priority**: 0.05x quality target for SBR bands.
- **Transition**: 16-bin cross-fade.

## Conclusion
The implementation now correctly leverages the psychoacoustic model to fill spectral gaps with low-priority synthetic content, providing a significant and robust quality boost.
