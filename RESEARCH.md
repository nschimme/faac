# Pseudo-SBR V3: Hybrid Bandwidth Extension Research

## Objective
Implement and optimize a Pseudo-SBR (Bandwidth Extension) system for FAAC-LC to improve audio quality at 32-48 kbps/channel.

## Final Implementation Strategy: "Hybrid Peak-Matching Stealth Fold"
Based on iterative testing and MOS validation, the following strategies were combined:

1. **Hybrid Crossover**: A frequency-based floor (10kHz) is set, but folding only begins at the first Scale Factor Band (SFB) above this floor that the LC encoder has zeroed out. This prevents overwriting high-quality "real" data while filling gaps where bit-starvation occurs.
2. **Harmonic Search (Peak-Matching)**: Cross-correlation with the original unquantized MDCT spectrum is used to find the optimal source window offset (±32 bins). This ensures the folded harmonics align with the original signal's structure, minimizing "chirping" and phasing artifacts.
3. **Adaptive SFM-Based Tilt**: The gain tilt is adjusted dynamically based on the Spectral Flatness Measure (SFM) of the source band:
   - **Tonal (Low SFM)**: -18dB at 16kHz (steeper tilt to prevent metallic ringing).
   - **Noisy (High SFM)**: -9dB at 16kHz (shallower tilt to fill out air).
   - **Base**: -12dB at 16kHz.
4. **Stealth Integration**: To minimize bitrate overhead, the folding is applied to the Sum (Mid) channel within the stereo processing loop. This allows Intensity Stereo (IS) logic to naturally propagate the folded energy to the Side channel using existing panning scales.
5. **Phase-Stable Transition**: A 3-bin linear fade-in is applied at the crossover point to prevent energy discontinuities and "spectral clicks."
6. **Comfort Noise Floor**: Small random noise (±0.005) is injected into folded bins to avoid harmonic sparsity.

## Benchmarking Results (32 kbps/channel)

| Scenario | Baseline MOS | SBR MOS | Delta | Result |
| :--- | :---: | :---: | :---: | :---: |
| **Music (Low Bitrate)** | 3.00 | 3.21 | **+0.21** | **SUCCESS** |
| **Speech (VSS)** | 3.75 | 3.77 | **+0.02** | **STABLE** |

## Findings
- **The Bitrate Trap**: Adding completely new SFBs at 32kbps often triggers a regression by stealing too many bits for side-information. The "Stealth" approach of filling zeroed bands within the existing bandwidth is critical for maintaining core clarity.
- **Harmonic Alignment**: Without Strategy 2 (Peak-Matching), tonal content (like piano or solo vocals) suffers from audible "doubling" or "chorusing" artifacts.
- **VisQOL Sensitivity**: Full-reference metrics like ViSQOL are highly sensitive to phase shifts in the high frequencies. The hybrid crossover ensures that we only intervene when the alternative is silence.
