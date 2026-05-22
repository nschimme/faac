# FAAC Quantization Thresholds & TNS Tuning

This document justifies the psychoacoustic thresholds and factors used in `libfaac/quantize.c`, as required by project maintenance standards. These values were derived via extensive feature sweeps using the `faac-benchmark` suite, targeting maximum perceptual quality (MOS) across VoIP, VSS, and Music scenarios.

## Quantization Constants

### `NOISEFLOOR` (0.10)
- **Purpose**: Defines the minimum RMS energy for a scale factor band to be considered non-silent.
- **Justification**: Balanced at 0.10 to ensure VoIP MOS is above baseline (>3.58) and preserve low-energy detail. Lowering this value was critical to resolving speech regressions while maintaining high-fidelity music performance.

## Masking Target Factors

### `NOISETONE` (0.2) and `TONEMASK` (0.45)
- **Purpose**: Weighting factors for the noise-like (average energy) and tone-like (peak energy) components of the masking target.
- **Justification**: Standard proportions that balance audio fidelity and bitrate stability.

### `SHORT_PENALTY` (0.45)
- **Purpose**: Tightens the masking target for short-window blocks.
- **Justification**: Ensures higher precision for rapidly changing signals (transients), reducing pre-echo artifacts.

### `SHORT_FLOOR_MULT` (1.5)
- **Purpose**: A multiplier applied to the masking floor for short blocks.
- **Justification**: Slightly loosens the floor for transients to avoid over-allocation of bits to extremely quiet high-frequency content.

## Energy Floor Factors (Divergence Prevention)

The `target_floor` logic prevents the masking target from collapsing on quiet upper bands, which would otherwise lead to aggressive truncation by the quantizer.

### `AVGE_FLOOR_FACTOR` (0.0001)
- **Derivation**: 10^( -40 dB / 10 ) = 0.0001.
- **Justification**: Clamps the band-to-frame average energy ratio at -40 dB. This ultra-low floor preserves high-frequency "air" in music and subtle speech nuances that were previously being truncated.

### `MAXE_FLOOR_FACTOR` (0.0010)
- **Derivation**: 10^( -30 dB / 10 ) = 0.0010.
- **Justification**: Clamps the peak energy ratio at -30 dB. Provides a safety net for tonal components in quiet bands.

## Feature Sweep Results (Summary)

| Configuration | VoIP MOS | VSS MOS | Music Std MOS | Notes |
| :--- | :---: | :---: | :---: | :--- |
| **nf0.10_af0.0001_mf0.0010** | **3.60** | **4.13** | **4.55** | **Optimal (Current)** |
| nf0.35_af0.001_mf0.005 | 3.57 | 4.14 | 4.50 | Lower VoIP Quality |
| nf0.40_af0.005_mf0.02 | 3.53 | 4.13 | 4.51 | Baseline-like |

*MOS scores computed via ViSQOL backend. All configurations use a unified masking formula protected by a frame-level energy gate to prevent division by zero.*
