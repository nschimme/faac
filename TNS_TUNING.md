# AAC-LC TNS Tuning Analysis

This document describes the derivation and verification of the Temporal Noise Shaping (TNS) thresholds in FAAC. These thresholds were tuned to maximize perceptual quality (MOS) while preventing the "killer" regressions (audible noise modulation on tonal content) that affected previous short-block TNS attempts.

## Threshold Sweep Results

A feature sweep was performed across a representative set of files using `faac-benchmark` with the ViSQOL perceptual metric.

| Threshold | Tested Range | Selected Value | Rationale |
|-----------|--------------|----------------|-----------|
| `TNS_GAIN_THRESH_LOW` | [1.2, 1.6] | **1.4** | Industry-standard lower bound (FFmpeg, libaacplus). Ensures prediction gain is sufficient to overcome bitstream overhead. |
| `TNS_GAIN_THRESH_HIGH` | [8.0, 20.0] | **12.0** | Prevents TNS from firing on steady-state tonal segments (e.g. speech formants) where it harms bitrate efficiency. |
| `TNS_FLATNESS_K` | [1.2, 2.0] | **1.5** | Minimum $L2^2 \cdot N / L1^2$ ratio. Filters out nearly-flat spectra where LPC prediction gain is spurious. |
| `TNS_PEAK_RATIO_MARGIN` | [1.0, 1.5] | **1.2** | Peak-to-mean gate. Skips TNS when the spectrum is too close to the Gaussian noise floor ($\sqrt{2 \ln N}$). |
| `TNS_ENERGY_FLOOR` | [0.01, 1.0] | **0.16** | Per-sample MDCT energy floor. Prevents TNS from wasting bits on essentially silent SFBs. |

### Perceptual Impact (ViSQOL MOS)

| Scenario | File | Baseline (TNS Off) | Candidate (TNS On) | Change |
|----------|------|-------------------|-------------------|--------|
| voip | C_03_ECHO_ML.wav | 3.86 | 3.88 | **+0.02** |
| voip | C_24_ECHO_ML.wav | 3.96 | 4.01 | **+0.05** |
| music_std | bah.wav | 4.29 | 4.29 | 0.00 |

## Implementation Details

### Spectral Whitening
To prevent TNS from fitting the spectral envelope (formants), the MDCT spectrum is normalized by the per-SFB energy before Levinson-Durbin analysis. This ensures that the prediction gain reflects temporal correlation (transients) within each band.

### Safety Epsilon
The `1e-30` floor used in the whitener (`1.0 / sqrt(e + 1e-30)`) is a standard safety floor to prevent division by zero in silent bands, consistent with high-quality AAC implementations in FFmpeg.

### Correct Indexing
Short blocks (8 windows of 128 samples) are correctly handled by using the `w * windowSize` offset for all spectral access and scratchpad buffers.
