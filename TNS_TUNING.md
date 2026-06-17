# FAAC Quantization Thresholds & TNS Tuning

This document justifies the psychoacoustic thresholds and factors used in `libfaac/quantize.c` and `libfaac/tns.c`, as required by project maintenance standards. These values were derived via extensive feature sweeps and validated against GitHub CI regression suites.

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
- **Rationale**: TNS provides measurable MOS gains only at bitrates where quantization noise is high enough to be reshaped. The gate was increased from 32 to 64 kbps/ch to enable TNS for the `vss` (40 kbps mono) and `music_40/48` (20-24 kbps/ch stereo) scenarios, which showed significant quality-impact regressions when TNS was disabled.
- **Implementation**: TNS is hard-disabled for `bitratePerCh >= 64000` and for quality mode (`bitrate == 0`).

### Fixed Order (8)
- **Rationale**: Replaces the adaptive ladder with a fixed order of 8 for long windows. This is MOS-neutral (+0.002) compared to higher orders at low bitrates but simplifies analysis.

### Break-Even Gain Thresholds
- **Rationale**: TNS utility is modeled as a break-even bit budget calculation (`spectral_bits` vs `tns_overhead`).
- **`TNS_THRESH_FLOOR` (1.10)**: Minimum LPC gain required to activate TNS. Lower values (e.g. 1.05) increase TNS frequency but risk bit starvation in the core quantizer on tonal content.
- **`TNS_THRESH_CAP` (1.80)**: Maximum adaptive threshold. Prevents TNS from being permanently disabled at very low bitrates by capping the penalty of TNS overhead bits.

### Pre-gate Logic
- **Rationale**: 98% of frames rejected by the flatness/peak pre-gate have raw LPC gain < 1.1. The gate prevents expensive analysis on frames where TNS cannot provide a win.
- **Flatness Gate**: Rejects frames where the spectral flatness (L2^2*N/L1^2) is already high.
- **Peak Gate**: Rejects frames with low peak-to-mean ratios that behave like Gaussian noise.

### Optimized Single-Pass Analysis
- **Rationale**: The expensive "Arm D2" (second LPC analysis pass) was removed. Throughput research showed that a single-pass whitened analysis, combined with DSP kernel optimizations (hoisted Autocorrelation clamping and branchless SFB whitening), provides equivalent quality with a ~9% total throughput speedup.
- **Whitening**: Scaling spectral lines by `1/sqrt(sfbEnergy)` before analysis focuses the filter on within-band correlation rather than global spectral tilt.

*MOS scores computed via ViSQOL backend. Throughput measured in relative speed units.*
