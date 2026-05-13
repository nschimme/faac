# Research: Resolving Energy Drops in Transients

## Problem Statement
High-pitched transients (e.g., vocal peaks) in FAAC-encoded audio exhibited severe energy drops (holes) where spectral components were zeroed out. Initial analysis showed an energy ratio of **~0.24** compared to the original signal at 0.75s in the provided sample.

## Root Causes
1. **Aggressive Noise Floor:** The legacy `NOISEFLOOR` (0.4) was too high, causing low-energy but perceptually relevant transient components to be discarded.
2. **Short-Block Penalty:** The encoder applied a `1.5x` error multiplier to short blocks, aggressively reducing bits during transients.
3. **Rate Control Feedback:** In high-complexity frames, the Rate Control (RC) could drop the quality factor (`fix`) too low, leading to "cascading zeroing" of bands.
4. **Safety Buffer Overflow:** A stack buffer `xitab` in `qlevel` was undersized (288 elements), leading to stack smashing on complex groups.

## Solution
A multi-pronged approach was taken to preserve energy without regressing on overall quality:

1. **Parameter Tuning:**
   - **`NOISEFLOOR` (0.4 -> 0.15):** Preserves subtle transient detail.
   - **Short-Block Multiplier (1.5 -> 1.0):** Balanced bit allocation for transients.
   - **`RC_DEADBAND_THRESHOLD` (0.05 -> 0.01):** Tightened bit-rate targeting.
   - **Rate Control Floor (`fix >= 0.5`):** Prevents the RC from zeroing out frames during transient peaks.

2. **Safety Improvements:**
   - Increased `xitab` buffer to `FRAME_LEN` (1024).
   - Added runtime bounds check with explicit zeroing (`HCB_ZERO`) if group size exceeds buffer.

## Results
- **Energy Retention:** The worst-case energy ratio improved from **0.24** to **0.78** in the identified problem section.
- **Perceptual Quality (MOS):**
  - High Bitrate (128kbps/ch): **4.65** (Excellent)
  - Low Bitrate (40kbps/ch): **3.56** (Good/Fair)
- **Stability:** No regressions in MOS compared to previous versions, while resolving the audible "energy holes".
