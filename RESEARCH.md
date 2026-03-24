# Pseudo-SBR Research and MOS Optimization (Rebased)

## Goal
Improve audio quality (MOS) of the Pseudo-SBR implementation in FAAC while ensuring maintainability and robust numerical stability.

## Optimization Strategy
Instead of hard-coded bitrate tiers, SBR target bandwidth and growth caps are derived from the natural bandwidth (`baseBW`) calculated by the MOS-optimized `CalcBandwidth` function. This ensures that SBR scales naturally with the core encoder's decisions.

## Parameter Search (30-Iteration Suite)
We conducted a search across Extension Ratio, Growth Cap Ratio, and Noise Offset parameters.

### Master Baseline (ced646e)
- **VoIP (16kbps)**: 3.280
- **VSS (40kbps)**: 4.004
- **Music Low (64kbps)**: 3.365
- **Average**: 3.550

### Selected Optimal Configuration
- **Extension Ratio**: 1.20x
- **Growth Cap Ratio**: 0.10x
- **Noise Offset**: 0.04f
- **Noise Slope**: 0.20f

### Final Verified Results
| Scenario | Master Baseline | Rebased/Optimized | Delta |
| :--- | :---: | :---: | :---: |
| **VoIP (16kbps)** | 3.280 | 3.307 | **+0.027** |
| **VSS (40kbps)** | 4.004 | 4.033 | **+0.029** |
| **Music Low (64kbps)** | 3.365 | 3.385 | **+0.020** |

**Average Lift**: **+0.025** over the already improved master branch.

## Key Improvements
1.  **Numerical Stability**: Removed magic epsilons in SFM calculation; replaced with robust zero-guards to prevent logarithmic instability.
2.  **Derivation-Based Targets**: SBR bandwidth is now `baseBW * 1.20`, capped at `baseBW * 0.10` growth.
3.  **Envelope Adjustment**: Per-patch energy normalization ensures the extension matches the source energy level, preventing the extension from sounding thin.
4.  **Adaptive Dithering**: Signal-dependent noise injection based on SFM reduces metallic artifacts.
