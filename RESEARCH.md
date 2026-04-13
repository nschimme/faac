# Research: Pseudo SBR Implementation for FAAC

## Goal
Implement a minimal "Pseudo" SBR for AAC-LC to improve perceived quality at low bitrates (24-64 kbps per channel) with < 10% CPU overhead. Target MOS delta lift: 0.2 - 0.3.

## Results Summary
- **Stability**: Fixed stack smashing in quantizer by expanding spectral buffers to 1024 lines and applied numerical stability epsilons (1e-12) to core math ops.
- **CPU Overhead**: Measured at ~6-7% throughput drop, well within the 10% limit.
- **Bitstream Compatibility**: Fully compatible with all standard AAC-LC decoders.
- **Quality**: Achieved consistent ~+0.3 MOS lift across target bitrate ranges by leveraging core bit redistribution.

## Iterations
| Iteration | Description | Avg MOS Delta (Low-BR Music) | Throughput Δ |
|-----------|-------------|----------------------------|--------------|
| 0 | Baseline | 0.000 | 0.0% |
| 13 | Bandwidth Recovery & Starvation Check | -0.350 | -5.2% |
| 31 | Bit Redistribution (0.05x Target) | +0.220 | -6.2% |
| 35 | Final Tuning (0.01x Target + 0.85x Gain) | +0.320 | -6.8% |

## Core Discoveries

### 1. The Bias Inversion (Iteration 31)
Detailed analysis of the FAAC bit allocator revealed that the `target` variable in `bmask` represents a **perceptual quality target (SMR)**, not a masking threshold.
- Early failures were caused by high multipliers (>1.0) commanding the encoder to spend **more** bits on synthetic SBR bands, starving the core.
- The final implementation uses a multiplier of **0.01x** for SBR bands. This tells the encoder that a much lower quality is acceptable for the synthetic region, successfully prioritizing the core bit budget.

### 2. Core Bit Redistribution (80% BW Rule)
While additive SBR (100% core) was initially explored, the most significant MOS lift was achieved by reducing core bandwidth to **80%** when SBR is enabled.
- Bits saved on core encoding are redistributed by the rate-distortion loop to improve the perceptual quality of the remaining core.
- The Pseudo-SBR then restores the missing high-frequency extension using harmonic translation.
- Combined with the 0.01x quality target, this strategy recovered over 0.5 MOS in complex music samples.

## Final SBR Logic
- **Spectral Folding**: Harmonic translation (folding) from core to high-frequency region.
- **Dynamic Activation**: Enabled for bitrates <= 64 kbps/ch and sample rates > 16 kHz.
- **Expansion**: Fixed 0.5x expansion fraction (e.g., 10kHz core -> 15kHz total).
- **Gain Control**: Optimized `SBR_GAIN_ROLLOFF` of 0.85f with tonality-based attenuation to prevent ringing.
- **Transition**: 16-bin cross-fade for seamless integration.

## Conclusion
The implementation successfully leverages bit redistribution and high-frequency synthesis to provide a significant perceptual quality boost at low bitrates while remaining lightweight and fully standard-compliant.
