# Stereo Coherence Optimization Sweep

## Methodology
To improve FAAC's stereo coherence, we replaced the legacy "Mono-Collapse" M/S strategy with a "True M/S" transform and implemented a dynamic Intensity Stereo (IS) floor. We conducted grid sweeps across two primary quality points to balance spatial fidelity (Coherence Error) with perceptual quality (MOS).

- **High Quality (q=4.0, ~128kbps):** Goal is to minimize coherence error without MOS loss.
- **Low Quality (q=1.0, ~64kbps):** Goal is to recover MOS while maintaining improved coherence compared to baseline.

## Parameter Search

### 1. Fixed Baseline (Legacy)
- IS Floor: 5.5 kHz (Fixed)
- M/S Strategy: Mono-Collapse (Always)
- **Results (64k):** MOS ~3.40, Err ~0.0436
- **Results (128k):** MOS ~4.49, Err ~0.0253

### 2. Pure "True M/S" (Preserve both channels)
- IS Floor: 5.5 kHz
- M/S Strategy: True M/S
- **Results (64k):** MOS ~3.32 (Regressed), Err ~0.0150 (Improved)
- **Results (128k):** MOS ~4.48 (Stable), Err ~0.0120 (Improved)

### 3. Adaptive Hybrid Strategy (Final Implementation)
- **IS Floor:** `5500 + 4500 * (quality - 0.5)` Hz.
- **M/S Collapse Gate:** `5.0 + 10.0 * (quality - 1.0)`, clamped `[5, 50]`.
  - At low bitrates (q=1.0), it uses aggressive collapse (`coll_thr=5.0`) to save bits for MOS.
  - At high bitrates (q=4.0), it uses conservative True M/S (`coll_thr=35.0`) for spatial image.

## Final Verification (Music Subset)

| Bitrate | Config | Avg MOS | Avg Coherence Err |
| :--- | :--- | :---: | :---: |
| 64 kbps | Baseline | 3.406 | 0.0436 |
| 64 kbps | **Adaptive** | **3.391** | **0.0385** |
| 128 kbps | Baseline | 4.491 | 0.0253 |
| 128 kbps | **Adaptive** | **4.437** | **0.0084** |

## Conclusions
The Quality-Adaptive Hybrid strategy provides a significant improvement in stereo coherence (~60% reduction in error at 128kbps) while maintaining MOS stability. The discovery and fix of the Intensity Stereo bitstream signaling bug (IS on Right channel `cr->book`) was critical for standard compliance and quality recovery.
