# TNS Tuning and Threshold Justification

This document provides the rationale for the thresholds used in the libfaac Temporal Noise Shaping (TNS) module. These values were determined through a combination of industry standards, theoretical analysis of noise statistics, and empirical performance sweeps using the ViSQOL perceptual metric.

## Activation Gating Thresholds

To prevent TNS from over-firing—which can lead to audible "warbling" on steady-state signals or "bit starvation" on complex content—a multi-stage pre-gate is applied before the expensive Levinson-Durbin analysis.

### 1. Energy Floor (`TNS_ENERGY_FLOOR = 0.16`)
- **Rationale:** Prevents TNS analysis on near-silent frames where prediction gain is mathematically volatile due to low signal-to-noise ratios.
- **Derivation:** Corresponds to a per-sample MDCT magnitude floor. At 16-bit PCM equivalent levels, this sits safely above the quantization noise floor but below audible signal levels.

### 2. Spectral Flatness (`TNS_FLATNESS_K = 1.5`)
- **Rationale:** TNS relies on spectral non-flatness (autocorrelation in the frequency domain) to work. If a spectrum is already flat, LPC prediction will fail to provide meaningful gain.
- **Derivation:** Based on the ratio $N \cdot \frac{L_2^2}{L_1^2}$. For a perfectly flat spectrum, this ratio is 1.0 (Cauchy-Schwarz). A threshold of 1.5 ensures the signal has enough "structure" for prediction to be worthwhile.

### 3. Tonality Gate (`TNS_PEAK_RATIO_MARGIN = 1.2`)
- **Rationale:** Identifies whether the spectrum is dominated by peaks (tonal) or is more noise-like.
- **Derivation:** For white Gaussian noise over $N$ bins, the expected peak-to-mean ratio is approximately $\sqrt{2 \ln N}$. TNS is skipped if $\frac{\max|X|}{\text{mean}|X|} < 1.2 \cdot \sqrt{2 \ln N}$. This sits just above the statistical threshold for noise, protecting stable speech and noise-like segments from TNS-induced artifacts.

## Prediction Gain Thresholds

### 1. Activation Gain (`DEF_TNS_GAIN_THRESH = 1.4`)
- **Rationale:** Standard AAC threshold. TNS is only enabled if the prediction gain is at least 1.4 (~1.5 dB).

### 2. High-Gain Cap (`TNS_GAIN_THRESH_HIGH = 12.0`)
- **Rationale:** Prevents TNS from firing on extremely tonal, steady-state signals (like pure sines).
- **Justification:** High prediction gain on steady-state signals suggests that the "transient" TNS is trying to capture is actually just a stationary harmonic. Applying TNS here often degrades quality by spreading quantization noise across the frame where it is no longer masked by the signal's temporal structure. 12.0 was found to be the "sweet spot" for preventing artifacts in tonal music.

## Implementation Details

### Spectral Whitening
The encoder now uses a per-SFB inverse-energy whitening step (equivalent to `CalcWeightedSpectrum` in other AAC implementations). This ensures that TNS focuses on **temporal transients** (fine structure) rather than the **spectral envelope** (formants), which is already handled by the scale factor bit allocator.
