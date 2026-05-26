# FAAC TNS Tuning and Speech Regression Fix

## Overview
To address MOS regressions in VSS (voice) samples identified in CI, the TNS implementation uses ultra-conservative thresholds and a higher frequency starting band. This ensures TNS provides the desired pre-echo control for transients and complex music while remaining completely transparent for harmonic speech.

## Fix: 4500Hz Dynamic minBand
The TNS analysis now targets frequencies starting at approximately **4500 Hz**. By avoiding the lower spectral regions where harmonic speech structure is most dense, we eliminate the risk of audible noise-reshaping artifacts ("burbling") in voice content.

## Ultra-Conservative Parameters
The following constants are tuned to prioritize speech transparency:
- **TNS_SPECTRAL_FRAC (0.15)**: Highly selective bit-budget threshold, ensuring TNS only fires when the prediction gain significantly outweighs the bitstream overhead.
- **TNS_ENERGY_FLOOR (1.50)**: Skips analysis on low-energy bands to save CPU and prevent TNS activity on noise.
- **TNS_FLATNESS_K (3.5)**: Stricter flatness requirement; TNS only attempts analysis on non-flat spectra.
- **Max Orders (Long: 6, Short: 3)**: Reduced base orders to maintain a low CPU footprint.

## Quantization Stability
The `bmask` loop in `libfaac/quantize.c` was refactored to perform energy flooring *before* the primary masking target calculation. This ensures that the psychoacoustic model remains stable in quiet bands and prevents spectral collapse across all scenarios.
