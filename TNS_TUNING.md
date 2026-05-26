# FAAC Quantization Thresholds & TNS Tuning

This document justifies the psychoacoustic thresholds and factors used in `libfaac/quantize.c` and `libfaac/tns.c`.

## Quantization Constants

### `NOISEFLOOR` (0.38)
- **Purpose**: Defines the minimum RMS energy for a scale factor band to be considered non-silent.
- **Justification**: Reduced from 0.40 to 0.38 to improve average MOS by preserving more low-energy spectral content, compensating for TNS bit overhead.

## TNS Analysis Gating (Pre-gate)

### `TNS_ENERGY_FLOOR` (0.75)
- **Purpose**: Minimum band energy to attempt TNS analysis.
- **Justification**: Prevents wasting CPU on low-energy frames where TNS gain is negligible.

### `TNS_FLATNESS_K` (2.2)
- **Purpose**: Spectral flatness threshold.
- **Justification**: Skips analysis on flat spectra where TNS provides little prediction gain.

## TNS Complexity Control

### Max Orders
- **Long Blocks**: 6 (Adaptive: 4-5 at high bitrates).
- **Short Blocks**: 3.
- **Justification**: Provides the majority of the MOS benefit of full-order TNS while keeping CPU overhead minimized.

### `TNS_SPECTRAL_FRAC` (0.50)
- **Purpose**: Threshold for TNS break-even gain calculation.
- **Justification**: Balanced threshold to ensure TNS fires only when the prediction gain is high enough to justify the bitstream overhead.
