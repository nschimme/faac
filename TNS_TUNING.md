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

*MOS scores computed via ViSQOL backend. Throughput measured in relative speed units.*

## TNS Tuning and Optimization (fix-tns-vss-regressions)

### Spectral Budget Gate (15% Long / 10% Short)
- **Purpose**: Prevents TNS from starving the core quantizer of bits, which is critical at low bitrates where TNS overhead (coefficients and side data) can be a significant fraction of the total budget.
- **Justification**: Validated via VoIP and VSS sweeps. Setting the gate to 10% for short windows and 15% for long windows maintains high core quality while allowing TNS to fire on frames where its gain is truly beneficial.

### Arm D1: Lossless Bitstream Compression
- **Purpose**: Signals `coefCompress = 1` in the bitstream when all quantized reflection coefficients for a filter fit within a 3-bit signed range ([-4, 3]).
- **Justification**: Reduces TNS side-info overhead by 1 bit per coefficient without any quality loss.

### Arm D2: Dual-Analysis Strategy
- **Purpose**: Performs LPC analysis on both the whitened spectrum (standard) and the raw MDCT spectrum.
- **Justification**: Standard whitened analysis can sometimes miss strong correlations in echoic or reverberant signals. If the raw spectrum analysis yields significantly higher prediction gain (`gain_raw > 1.05 * gain_whitened`), raw coefficients are used. This strategy recovered quality on reverberant speech samples (e.g., C_24 files) that previously regressed.

### Encoder-Decoder Synchronization (stopBand)
- **Purpose**: Ensures `stopBand` is always synchronized with `maxSfb`.
- **Justification**: AAC decoders apply TNS filters starting from the highest active scale factor band downwards. Any mismatch in the starting band between the encoder and decoder results in a frequency shift of the spectral noise reshaping, causing severe audible artifacts ("spectral garbage").
