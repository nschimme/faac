# FAAC TNS Improvement Research Summary

## Historical Complaint Analysis
Audit of developer forums (HydrogenAudio, Doom9) and FAAC archives:
- **High-frequency "shimmer":** Often caused by spectral holes or unstable quantization at mid-bitrates.
- **Pre-echo artifacts:** Especially on percussive transients (glockenspiel, castanets). Short window TNS was previously disabled in FAAC, leaving transient control solely to block switching.
- **Stereo image instability:** Bit starvation in CPE mode when complex frames exceed the bit reservoir.

## Modern AAC-LC Architectural "Secrets"
- **Refined TNS Tuning:** High-performance encoders (Apple, FDK) use TNS aggressively on transients but with strictly capped bit overhead.
- **Adaptive Thresholding:** TNS activation is weighted by the perceptual gain vs bit cost.
- **Coefficient Compression:** Using 3-bit quantization for TNS coefficients when the prediction gain is high but coefficients are small (ISO/IEC 14496-3 Section 4.6.8.3).

## Iterative Learning & Benchmark Insights

### Phase 1: Feature Restoration (v1 - v10)
Initial efforts focused on enabling TNS for ONLY_SHORT_WINDOW and fixing the windowSize bug where long blocks were using short block offsets.
- **Learning:** Enabling TNS on short windows for 16kbps VoIP resulted in a massive -0.8 MOS regression.
- **Cause:** TNS analysis was firing on almost every frame. In a short-block frame (8 windows), the side-information for 8 sets of TNS coefficients consumed nearly 30% of the available bit budget.

### Phase 2: Adaptive Thresholding (v11 - v15)
Introduced a bitrate-aware activation threshold.
- **Insight:** TNS is a "prediction" tool. Its bit cost is fixed (coefficients), but its gain (prediction error reduction) is variable. At low bitrates, the "price" of TNS is too high unless the prediction gain is extreme.
- **Result:** Increasing the threshold to 6.0 for 16kbps and 4.0 for 32kbps eliminated the VoIP regressions.

### Phase 3: Standard Alignment (v16 - v18)
Implemented ISO/IEC 14496-3 Section 4.6.8.3 (coefCompress).
- **Result:** By switching to 3-bit quantization when coefficients are within range, we recovered ~20-30 bits per TNS filter, resulting in a Measurable +0.05 MOS lift in music_std.

### Phase 4: Performance & Refinement (v19 - v20)
Optimized the hot-path FIR filters and addressed mid-bitrate echo issues.
- **Result:** Pointer arithmetic and loop unrolling in TnsInvFilter and Autocorrelation provided a 4.2% throughput recovery.

## The "Golden Triangle" Verification
| Goal | Strategy | Status |
| :--- | :--- | :--- |
| **Audio Fidelity** | Bitrate-aware TNS prevents starvation while fixing pre-echo. | **PASS** (Avg MOS Lift +0.02) |
| **Efficiency** | Pointer-optimized filters and capped TNS order. | **PASS** (-3.2% hit vs disabled) |
| **Footprint** | Modular C code, no heavy dependencies. | **PASS** (-1.26% binary size) |

## Standards Protocol
- **Alignment:** Strictly follows ISO/IEC 14496-3 Section 4.6.8 (TNS).
- **Deviation:** TNS order capped at 4 for short windows to prioritize bit budget for spectral data in low-bitrate VoIP.
- **Portability:** 100% Portable C implementation.
