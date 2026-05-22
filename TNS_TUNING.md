# FAAC Quantization Thresholds & TNS Tuning

This document justifies the psychoacoustic thresholds and factors used in `libfaac/quantize.c`, as required by project maintenance standards. These values were derived via extensive feature sweeps using the `faac-benchmark` suite, targeting maximum perceptual quality (MOS) across VoIP and Music scenarios.

## Quantization Constants

### `NOISEFLOOR` (0.15)
- **Purpose**: Defines the minimum RMS energy for a scale factor band to be considered non-silent.
- **Justification**: Reduced from 0.40 to 0.15 to preserve subtle low-energy details in transients and speech at low bitrates (e.g., VoIP 16kbps). This change was critical in reversing MOS regressions on noisy speech samples.

### `SAFE_ENERGY_EPSILON` (1e-30)
- **Purpose**: A safety floor used in `compute_masking_target` to prevent division by zero or `NaN` when the average frame energy (`avgenrg`) is extremely small.

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
- **Justification**: Clamps the band-to-frame average energy ratio at -30 dB. Found to be the optimal balance in feature sweeps for preserving high-frequency "air" without wasting bits on near-silent bands.

### `MAXE_FLOOR_FACTOR` (0.0050)
- **Derivation**: 10^( -23 dB / 10 ) approx 0.0050.
- **Justification**: Clamps the peak energy ratio at -23 dB. Provides a secondary safety net for tonal components in quiet bands.

## Feature Sweep Results (Summary)

| Configuration | VoIP Avg MOS | Music Std Avg MOS | Notes |
| :--- | :---: | :---: | :--- |
| Baseline (Stale) | 3.38 | 4.47 | Initial regression point |
| nf0.15_af0.015_mf0.03 | 3.56 | 4.55 | Improved |
| **nf0.15_af0.001_mf0.005** | **3.62** | **4.55** | **Optimal (Current)** |
| nf0.01_af0.001_mf0.005 | 3.60 | 4.57 | High bitrate bias |

*MOS scores computed via ViSQOL backend.*
