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

## TNS Tuning and Optimization

### Dynamic Frequency Gating (4500Hz)
- **Purpose**: Restricts TNS operation to frequencies above 4500Hz.
- **Justification**: Harmonic speech signals often show high prediction gain in lower frequency regions, but applying TNS noise-reshaping there can cause audible artifacts on vowels. Dynamically finding the SFB closest to 4500Hz based on the sample rate protects speech quality while allowing TNS to work on transient "clicks" and high-frequency noise.

### Adaptive Spectral Budget Gate (15% Long / 10% Short)
- **Purpose**: Prevents TNS from starving the core quantizer of bits at low bitrates.
- **Justification**: TNS side-info (coefficients) is a fixed cost. When this cost exceeds 15% (long) or 10% (short) of the estimated spectral bit budget, the `gainThresh` is adaptively increased. This ensures TNS only fires when its benefit is high enough to justify the bit cost.

### Arm D1: Lossless Bitstream Compression
- **Purpose**: Signals `coefCompress = 1` in the bitstream when all quantized reflection coefficients for a filter fit within a 3-bit signed range ([-4, 3]).
- **Justification**: Reduces TNS side-info overhead by 1 bit per coefficient without any quality loss.

### Arm D2: Dual-Analysis Strategy
- **Purpose**: Performs LPC analysis on both the whitened spectrum (standard) and the raw MDCT spectrum.
- **Justification**: Standard whitened analysis can miss strong correlations in echoic or reverberant signals. If raw spectrum analysis yields >15% higher prediction gain, raw coefficients are used. This recovered quality on reverberant speech samples (e.g., C_24 files).

### Encoder-Decoder Synchronization (stopBand)
- **Purpose**: Ensures `stopBand` is always synchronized with `maxSfb`.
- **Justification**: AAC decoders apply TNS filters starting from the highest active scale factor band downwards. Any mismatch results in a frequency shift of the noise reshaping, causing severe distortion.

## Feature Sweep Results (Summary)

| Configuration | VoIP Avg MOS | VSS Avg MOS | Music Low Avg MOS | Overall MOS | Throughput |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **Integrated Baseline** | **3.62** | **4.21** | **3.35** | **3.91** | **3.4x** |
| Refined TNS | 3.70 | 4.22 | 3.35 | 3.92 | 3.4x |

*MOS scores computed via ViSQOL backend. Throughput measured in relative speed units.*
