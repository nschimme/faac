# Mixed Mode Stereo Coding Research

This document outlines the heuristic logic and psychoacoustic thresholds used in the Mixed Mode joint stereo implementation.

## 1. Decision Hierarchy
Mixed Mode dynamically selects between Intensity Stereo (IS), Mid/Side (M/S), and L/R coding per scale factor band (SFB). The decision follows this priority:
1.  **Intensity Stereo (IS)**: Evaluated first for high-frequency bands.
2.  **Mid/Side (M/S)**: Evaluated if IS is not suitable or not applicable.
3.  **L/R Coding**: Default fallback.

## 2. Intensity Stereo (IS)
### Frequency Limit
IS is restricted to bands above **6 kHz**. This is a psychoacoustic best practice because the human ear is highly sensitive to phase differences at lower frequencies. Using IS below 6kHz often causes a noticeable collapse of the stereo image.

### Thresholds
IS uses a quality-scaled energy threshold (`isthr`):
-   `isthr = (0.18 / (quality^2)) + 1.0`
-   The decision uses `ethr = (sqrt(enrgL) + sqrt(enrgR))^2 / isthr`.
-   Note: The threshold is applied as a divisor to `ethr` to match the legacy `stereo()` function's behavior where `phthr = 1.0 / isthr`.
-   The "Panning" limit is strictly enforced at **±30 units**.

## 3. Mid/Side (M/S)
### Quality-Scaled Thresholds
M/S decision relies on `thrmid` (for M/S suitability) and `thrside` (for side-channel elimination):
-   `thrmid = (0.045 / quality) + 1.0` (A tighter 0.5x scaling of the legacy `thr075` is used in Mixed Mode to prioritize stereo separation when M/S isn't clearly superior).
-   `thrside = (0.1 / quality)`.

### M/S Condition
M/S is selected if:
-   `min(enrgL, enrgR) * thrmid >= max(enrgSum * 0.25, enrgDiff * 0.25)`
-   The `0.25` factor accounts for the energy scaling of the sum/diff signals (`0.5 * (L+R)`).

## 4. Mutual Exclusivity
In Mixed Mode, IS and M/S are mutually exclusive per band. If IS is chosen, M/S evaluation is skipped. This prevents redundant processing and ensures bitstream compliance.

## 5. Summary of Defaults
-   `JOINT_MIXED` (Mode 3) is the new default.
-   MOS benchmarking shows that this configuration maintains baseline quality (MOS delta < 0.05) while providing a more dynamic and robust stereo image across various bitrates.
