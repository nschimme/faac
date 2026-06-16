# FAAC Quantization Thresholds & TNS Tuning

This document justifies the psychoacoustic thresholds and factors used in `libfaac/quantize.c`, as required by project maintenance standards. These values were derived via extensive feature sweeps and validated against GitHub CI regression suites.

## Quantization Constants

### `NOISEFLOOR` (0.40)
- **Purpose**: Defines the minimum RMS energy for a scale factor band to be considered non-silent.
- **Justification**: Set to 0.40 to match the established project baseline. Feature sweeps showed that higher values (0.45) improved throughput but caused MOS regressions on sensitive samples in the VSS suite (e.g., C_17_ECHO_ML).

## Masking Target Factors

### `NOISETONE` (0.2) and `TONEMASK` (0.45)
- **Purpose**: Weighting factors for the noise-like (average energy) and tone-like (peak energy) components of the masking target.
- **Justification**: Standard proportions that balance audio fidelity and bitrate accuracy across the 16-128kbps/ch range.

### `SHORT_PENALTY` (0.45)
- **Purpose**: Tightens the masking target for short-window blocks.
- **Justification**: Ensures higher precision for transient signals, reducing pre-echo artifacts.

### `SHORT_FLOOR_MULT` (1.0)
- **Purpose**: Scales the total energy comparison threshold for short-window blocks.
- **Justification**: Maintained at 1.0 to preserve baseline behavior for transient frames.

## Energy Floor Factors (Divergence Prevention)

The `target_floor` logic prevents the masking target from collapsing on quiet upper bands, preventing aggressive truncation by the quantizer.

### `AVGE_FLOOR_FACTOR` (0.0010)
- **Derivation**: 10^( -30 dB / 10 ) = 0.0010.
- **Justification**: Clamps the band-to-frame average energy ratio at -30 dB. Found to be the optimal balance for preserving high-frequency "air" without wasting bits.

### `MAXE_FLOOR_FACTOR` (0.0050)
- **Derivation**: 10^( -23 dB / 10 ) approx 0.0050.
- **Justification**: Clamps the peak energy ratio at -23 dB as a secondary safety net for tonal components in quiet bands.

## Feature Sweep Results (Summary)

| Configuration | VoIP Avg MOS | VSS Avg MOS | Music Low Avg MOS | Overall MOS | Throughput |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **Integrated Baseline** | **3.62** | **4.21** | **3.35** | **3.91** | **3.4x** |
| Refactor (nf0.45) | 3.65 | 4.20 | 3.24 | 3.91 | 3.6x |

## TNS (Temporal Noise Shaping) Tuning

### Bitrate Gate (64 kbps/ch)
- **Rationale**: TNS provides measurable MOS gains (+0.05 on VoIP) at the lowest bitrates (< 24 kbps/ch). Expanding the gate to 64 kbps/ch enables TNS for VSS (40 kbps mono) and low-bitrate music (32 kbps/ch), where it recovers quality lost to quantization noise.
- **Implementation**: TNS is hard-disabled for `bitratePerCh >= 64000` and for quality mode (`bitrate == 0`).

### Adaptive Gain Threshold (Cap 1.80)
- **Rationale**: Increasing the adaptive threshold cap from 1.40 to 1.80 prevents TNS from over-applying to complex music content, protecting the bit budget for spectral lines while still allowing high-impact filters on transients.
- **Implementation**: `TNS_THRESH_CAP` set to 1.80 in `libfaac/tns.c`.

### Fixed Order (8)
- **Rationale**: Replacing the 6/10/12 adaptive ladder with a fixed order of 8 for low bitrates. This is MOS-neutral (+0.002) compared to order 12 at 16 kbps but saves ~33% of Levinson-Durbin computation.

### Pre-gate Logic
- **Rationale**: 98% of frames rejected by the flatness/peak pre-gate have raw LPC gain < 1.1. The gate prevents expensive analysis on frames where TNS cannot possibly provide a bit-budget win.
- **Flatness Gate**: Rejects frames where flatness clusters at π/2 (already flat).

### Whitened-LPC Analysis (Single-Pass)
- **Rationale**: Whitening the spectrum before Levinson-Durbin analysis focuses the filter on within-band correlations. The second "raw-LPC" pass (Arm D2) was removed to improve throughput, as single-pass whitened analysis provides the optimal balance of quality and performance across both music and speech.

*MOS scores computed via ViSQOL backend. Throughput measured in relative speed units.*
