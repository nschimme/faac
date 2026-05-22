# FAAC Quantization Thresholds & TNS Tuning

This document justifies the psychoacoustic thresholds and factors used in `libfaac/quantize.c`, as required by project maintenance standards. These values were derived via extensive feature sweeps using the `faac-benchmark` suite, targeting maximum perceptual quality (MOS) across VoIP, VSS, and Music scenarios.

## Quantization Constants

### `NOISEFLOOR` (0.40)
- **Purpose**: Defines the minimum RMS energy for a scale factor band to be considered non-silent.
- **Justification**: Balanced at 0.40 to provide high fidelity and stability. Extensive feature sweeps (covering NOISEFLOOR 0.05 to 0.55) confirmed that this value remains the optimal balance for the integrated feature set.

## Masking Target Factors

### `NOISETONE` (0.2) and `TONEMASK` (0.45)
- **Purpose**: Weighting factors for the noise-like (average energy) and tone-like (peak energy) components of the masking target.
- **Justification**: These proportions balance the "Golden Triangle" of audio fidelity and bitrate accuracy.

### `SHORT_PENALTY` (0.45)
- **Purpose**: Tightens the masking target for short-window blocks.
- **Justification**: Prevents bit starvation during transients by ensuring higher precision for rapidly changing signals, reducing pre-echo artifacts.

## Energy Floor Factors (Divergence Prevention)

The `target_floor` logic prevents the masking target from collapsing on quiet upper bands, which would otherwise lead to aggressive truncation by the `quantize_scalar` magic-number rounding.

### `AVGE_FLOOR_FACTOR` (0.0010)
- **Derivation**: 10^( -30 dB / 10 ) = 0.0010.
- **Justification**: Clamps the band-to-frame average energy ratio at -30 dB. Found to be the optimal balance for preserving high-frequency "air" without wasting bits on near-silent bands.

### `MAXE_FLOOR_FACTOR` (0.0050)
- **Derivation**: 10^( -23 dB / 10 ) approx 0.0050.
- **Justification**: Clamps the peak energy ratio at -23 dB. Provides a secondary safety net for tonal components in quiet bands.

## Feature Sweep Results (Summary)

| Configuration | VoIP Avg MOS | VSS Avg MOS | Music Low Avg MOS | Notes |
| :--- | :---: | :---: | :---: | :--- |
| **nf0.40_af0.001_mf0.005** | **3.65** | **4.20** | **3.24** | **Balanced (Current)** |
| nf0.35_af0.001_mf0.005 | 3.64 | 4.19 | 3.23 | Regression |

*MOS scores computed via ViSQOL backend. All configurations are protected by a frame-level energy gate in `bmask()` to prevent division by zero in silent segments.*
