# FAAC TNS Tuning and Speech Safety

## Overview
TNS (Temporal Noise Shaping) is enabled by default to improve quality on transient signals. To prevent audible artifacts on stationary harmonic content (like speech), several safety measures are implemented.

## Safety Measures

### 1. Spectrum Whitening
`WhitenSpectrumForTns` performs per-SFB energy normalization and smoothing. This ensures that the Levinson-Durbin recursion measures within-band temporal correlation rather than inter-band formant structure.

### 2. Dynamic Frequency Gating (4500Hz)
TNS is only applied to frequencies above ~4.5kHz. This avoids noise-reshaping in the critical lower frequency range where speech formants are most prominent.

### 3. Pre-Analysis Gating
- **TNS_ENERGY_FLOOR (1.5)**: Skips LD for silent/quiet bands.
- **TNS_FLATNESS_K (3.5)**: Skips LD for already flat spectra.
- **TNS_SPECTRAL_FRAC (0.15)**: Conservative bit-budget allocation.

## Parameters
- **Base Orders**: Long blocks use order 6 (adaptive down to 4), short blocks use order 3.
- **NOISEFLOOR**: 0.40 (Standard project baseline).
