# FAAC TNS Improvement Research Summary

## Historical Complaint Analysis
Audit of developer forums (HydrogenAudio, Doom9) and FAAC archives:
- **High-frequency "shimmer":** Often caused by spectral holes or unstable quantization at mid-bitrates.
- **Pre-echo artifacts:** Especially on percussive transients (glockenspiel, castanets). Short window TNS was previously disabled, leaving transient control solely to block switching.
- **Stereo image instability:** Bit starvation in CPE mode when complex frames exceed the bit reservoir.

## Modern AAC-LC Architectural "Secrets"
- **Refined TNS Tuning:** High-performance encoders (Apple, FDK) use TNS aggressively on transients but with strictly capped bit overhead.
- **Adaptive Thresholding:** TNS activation is weighted by the perceptual gain vs bit cost.
- **Coefficient Compression:** Using 3-bit quantization for TNS coefficients when the prediction gain is high but coefficients are small (ISO/IEC 14496-3 Section 4.6.8.3).

## The "Golden Triangle" Strategy
1. **Audio Fidelity:** Restore TNS for short windows to eliminate pre-echo.
2. **Efficiency:** Optimized `TnsInvFilter` and `Autocorrelation` while maintaining full portability.
3. **Minimal Footprint:** Capped short-window TNS order at 4 and used `coefCompress` to keep bitstream overhead below 1% per frame.

## Standards Protocol
- **Alignment:** Strictly follows ISO/IEC 14496-3 Section 4.6.8 (TNS).
- **Deviation:** TNS order capped at 4 for short windows (standard allow 7) to prioritize bit budget for spectral data in low-bitrate VoIP.
- **Portability:** 100% Portable C implementation.
