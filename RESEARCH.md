# Energy Drop Analysis (Issue #40)

## Problem Statement
Decoded audio exhibited significant energy drops during transient segments (RMS ratios as low as 0.24), particularly during short-block transitions.

## Investigation & Findings
1.  **Aggressive Noise Floor:** The default `NOISEFLOOR` of 0.4 was zeroing out spectral bands during low-quality frames (transients).
2.  **Short Block Penalty:** Short blocks were penalized with a higher error target multiplier (1.5x), leading to bit starvation and energy loss.
3.  **Buffer Safety:** Identified a stack smashing vulnerability in `qlevel` (xitab buffer size).

## Solution
-   **Noise Floor:** Reduced to 0.15 (Balanced signal retention vs. bit demand).
-   **Quality Allocation:** Improved short-block quality by reducing error multiplier to 1.0 (from 1.5). This provides enough energy retention (0.79 ratio) while avoiding MOS regressions at low bitrates.
-   **Stability:** Increased `xitab` size to `FRAME_LEN` (1024) and added boundary guards.
-   **Rate Control:** Tighter deadband (0.01) for more precise bit budget management.

## Verification
Minimum energy ratio on provided sample improved from **0.24 to 0.79**. MOS scores remain identical to baseline (e.g., 3.56 at 40kbps, 4.65 at 128kbps).
