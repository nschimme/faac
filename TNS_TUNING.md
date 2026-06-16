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

## TNS (Temporal Noise Shaping) Tuning

### Bitrate Gate (64 kbps/ch)
- **Rationale**: TNS coverage is extended to 64 kbps/ch to encompass the `vss` (40 kbps mono) and low-bitrate music scenarios. This ensures TNS utility for challenging acoustic environments and complex stereo signals where quantization noise still exceeds masking floors.
- **Implementation**: TNS is disabled for `bitratePerCh >= 64000` and for transparency-targeted quality mode (`bitrate == 0`).

### Break-Even Thresholds
- **Rationale**: Adaptive gain thresholds (`gainThreshLong/Short`) are derived from a break-even bit budget model.
- **`TNS_THRESH_FLOOR` (1.40)**: Minimum gain required for TNS activation. Tuned to 1.40 to prevent over-aggressive activation on reverberant speech (e.g. `vss` suite) while allowing coverage for transients.
- **`TNS_THRESH_CAP` (1.80)**: Maximum threshold cap to ensure TNS activates on high-dynamic frames despite bit-starvation overhead at low bitrates.

### Loop Optimizations
- **Rationale**: CPU throughput is improved via portable code patterns:
  - **Hoisted Clamping**: Clamping logic in `Autocorrelation` is hoisted to the loop tail to improve L1 cache locality for the bulk of spectral data.
  - **Branchless Whitening**: `WhitenSpectrumForTns` utilizes a per-SFB weighting structure to eliminate conditional branches in the hot inner loop.
  - **Keyword Qualifiers**: Use of `restrict` and `const` enables aggressive compiler vectorization.

### Dual-Analysis Pass (Arm D2)
- **Rationale**: A second Levinson-Durbin pass on the raw spectrum is conditionally applied if it yields significant gain. This "safety pass" specifically rescues MOS quality for reverberant or echoic content where spectral whitening might under-represent the correlation structure.

### Short-Block TNS
- **Rationale**: Short-window TNS is disabled in the analysis path for music/vss scenarios to balance the throughput budget, as long-window TNS provides the majority of perceptual gains for these content types.

*MOS scores computed via ViSQOL backend. Throughput measured in relative speed units.*
