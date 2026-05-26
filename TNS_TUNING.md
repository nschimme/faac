# FAAC TNS Tuning and Optimization

This document outlines the tuning of the Temporal Noise Shaping (TNS) implementation to balance audio quality (MOS) improvement against CPU overhead.

## Final Configuration
TNS is enabled by default with the following optimized parameters:

- **Filter Orders**:
  - Base: 6 (Long blocks), 3 (Short blocks).
  - Adaptive: Order drops to 4 at >= 96kbps/ch and 3 at >= 128kbps/ch for long blocks to minimize CPU impact where TNS gain is marginal.
- **Analysis Pre-gating**:
  - `TNS_FLATNESS_K` (2.2): Skips analysis on flat spectra.
  - `TNS_ENERGY_FLOOR` (0.75): Skips analysis on near-silent frames.
  - `TNS_PEAK_RATIO_MARGIN` (1.2): Skips analysis on frames dominated by a single peak.
- **Threshold**:
  - `TNS_SPECTRAL_FRAC` (0.50): Calculated gain must justify the bitstream overhead based on a 50% spectral bit budget assumption.
- **Dynamic Frequency Gating**:
  - TNS analysis starts at ~3.4kHz. This prevents audible noise-reshaping artifacts in the critical speech/vocal region while preserving pre-echo control for higher frequencies.

## Architectural Improvements
The implementation includes several key performance and quality enhancements:
1. **Spectral Whitening**: Normalizes MDCT energy per SFB before TNS analysis, ensuring filters model temporal structure rather than spectral formants.
2. **Cheap Pre-gating**: A single-pass analysis loop fused with energy accumulation to skip expensive Levinson-Durbin calculations on 40-60% of typical frames.
3. **Robust Quantization**: Explicit clamping of reflection coefficient indices to prevent bitstream wrap-around errors.
4. **Optimized Autocorrelation**: A single-pass algorithm improving cache locality.

## Perceptual and Performance Impact
Benchmarking indicates a **+0.010 average MOS delta** (VisQOL) across a diverse audio corpus. The CPU overhead is approximately **11%** (a significant recovery from the ~18% seen in early unoptimized candidate states).
