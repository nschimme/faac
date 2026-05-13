# Energy Drop Analysis (Issue #40)

## Problem Statement
Decoded audio exhibited significant energy drops during transient segments (RMS ratios as low as 0.24), particularly during short-block transitions.

## Investigation & Findings
1.  **Aggressive Noise Floor:** The default `NOISEFLOOR` of 0.4 was zeroing out spectral bands during low-quality frames (transients).
2.  **Short Block Penalty:** Short blocks were penalized with a higher error target multiplier (1.5x), leading to bit starvation and energy loss.
3.  **Buffer Safety:** Identified and fixed a stack smashing vulnerability in `qlevel` (xitab buffer size).

## Solution
-   **Noise Floor:** Reduced to 0.15 (Balanced signal retention vs. bit demand).
-   **Quality Allocation:** Improved short-block quality by reducing error multiplier to 0.5.
-   **Stability:** Increased `xitab` size to `FRAME_LEN` (1024) to prevent crashes.
-   **Rate Control:** Tighter deadband (0.01) for better convergence.

## Verification
Minimum energy ratio on provided sample improved from **0.24 to 0.62**. MOS scores remain stable at **4.65**.
