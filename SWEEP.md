# Stereo Coding Parameter Sweep Results (High Coverage)

To resolve MOS regressions while maintaining stereo coherence improvements, a comprehensive sweep was performed across the music dataset.

## 1. Grid Search Parameters
- **IS Floor Scale:** 4500 (7.7kHz @ 128k) vs 9000 (10kHz @ 128k)
- **M/S Multiplier:** 0.85 (Legacy) vs 1.0 (Neutral) vs 1.15 (Conservative)
- **Hard Mono Threshold (`sidemin`):** 0.05 vs 0.1 (Legacy)

## 2. Quantitative Results (Avg across Core Music Subset)
*Scenario: music_std (128 kbps)*

| Configuration | Avg MOS | Avg Coherence Error | Avg Size (Bytes) |
| :--- | :---: | :---: | :---: |
| **f=4500, m=1.0, s=0.1** | **4.4383** | **0.0884** | 161,871 |
| f=4500, m=1.0, s=0.05 | 4.4378 | 0.0884 | 161,866 |
| f=9000, m=1.0, s=0.1 | 4.4344 | 0.0884 | 161,854 |
| Legacy (v1.31) | ~3.5 | ~0.15 | - |

**Observation:** Lowering the IS floor scale from 9000 to 4500 recovered ~0.004 MOS points by freeing up bit budget for the core spectrum, while still maintaining superior coherence compared to the legacy fixed floor. Removing the 0.85x M/S penalty (`ms_mult=1.0`) further improved MOS by allowing more frequent use of the True M/S domain.

## 3. Final Pareto Point Implementation
The following constants are now hardcoded in `libfaac/stereo.c`:

- **IS Floor:** `5500 + 4500 * (quality - 0.5)` Hz.
  - ~7.75 kHz floor at 128kbps (q=1.0).
  - ~5.5 kHz floor at 64kbps (q=0.5).
- **M/S Multiplier:** `1.0` (Neutral preference).
- **True M/S:** Replaces mono-collapse to preserve spatial image.
- **Hard Mono (`sidemin`):** `0.1` (-20dB suppression).

## Summary
The updated strategy provides a significant improvement in stereo image fidelity over the legacy encoder while ensuring MOS stability by dynamically balancing the phase-preservation vs. bit-budget trade-off.
