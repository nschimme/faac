# FAAC Quantization Thresholds & TNS Tuning

This document justifies the psychoacoustic thresholds and factors used in `libfaac/quantize.c` and `libfaac/tns.c`.

## TNS Analysis Gating (Pre-gate)

### `TNS_ENERGY_FLOOR` (1.00)
- **Purpose**: Minimum band energy to attempt TNS analysis.
- **Justification**: Prevents wasting CPU on low-energy frames where TNS gain is negligible. High value ensures efficiency.

### `TNS_FLATNESS_K` (3.0)
- **Purpose**: Spectral flatness threshold.
- **Justification**: Skips analysis on flat spectra where TNS provides little prediction gain.

## TNS Complexity Control

### Max Orders
- **Long Blocks**: 4 (Adaptive: 2-3 at high bitrates).
- **Short Blocks**: 2.
- **Justification**: Balanced order that provides temporal noise shaping benefit while keeping CPU overhead extremely low.

### `TNS_SPECTRAL_FRAC` (0.15)
- **Purpose**: Threshold for TNS break-even gain calculation.
- **Justification**: Highly selective threshold to ensure TNS only fires when the prediction gain is high enough to justify the bitstream overhead.
