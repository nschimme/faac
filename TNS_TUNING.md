# FAAC TNS Tuning and Speech Regression Fix

## Overview
Recent CI runs identified MOS regressions in VSS (voice) samples when TNS was enabled. Analysis revealed that applying TNS to lower frequencies (below ~3kHz) can introduce audible "burbling" or noise reshaping artifacts on harmonic speech content.

## Fix: Dynamic 3.4kHz minBand
The TNS implementation has been updated to dynamically calculate the starting scale factor band (`minBand`) based on the input sample rate, targeting approximately **3.4kHz**. This ensures that TNS only operates on higher frequencies where pre-echo control is most beneficial and speech artifacts are minimized.

## Balanced Parameters
To maintain quality improvements in music while remaining conservative for speech, the following parameters are used:
- **TNS_SPECTRAL_FRAC (0.50)**: A balanced threshold for TNS activity.
- **Max Orders (Long: 8, Short: 4)**: Increased from the previous conservative settings to recover some quality gain in music, with adaptive reduction at higher bitrates.
- **Adaptive Orders**: Long-block order is reduced to 6 at >= 96kbps/ch and 4 at >= 128kbps/ch.

## Verification
This configuration specifically addresses the speech regressions seen in CI while preserving the objective of having TNS enabled by default for a general quality improvement.
