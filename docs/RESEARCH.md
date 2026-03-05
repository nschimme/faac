# FAAC Research Summary: Historical Constraints vs. Modern Encoder Advantages

## 1. Historical Complaint Analysis (HydrogenAudio, Doom9, SourceForge)

An audit of developer forums and the FAAC archive reveals several persistent quality "pain points" that have historically limited FAAC's performance compared to contemporary encoders like Apple AAC or FDK-AAC.

### A. Metallic Ringing & High-Frequency Shimmer
*   **Symptom:** A "metallic" or "watery" texture in high-frequency content, particularly audible in cymbals, hi-hats, and speech sibilants at mid-range bitrates (40-64 kbps).
*   **Root Cause:** Coarse quantization of high-frequency spectral lines and poor Absolute Threshold of Hearing (ATH) modeling. When bits are scarce, the encoder often "collapses" high-frequency bands or quantizes them so aggressively that the resulting error manifests as a periodic metallic ring.
*   **Historical Context:** FAAC's internal psychoacoustic model (Knipsycho) often fails to allocate enough bits to preserve the "air" in high-frequency bands, leading to a perceived collapse of the soundstage.

### B. Pre-echo Artifacts on Transients
*   **Symptom:** A "smearing" or "fuzziness" immediately preceding sharp percussive sounds (e.g., castanets, drums).
*   **Root Cause:** Insufficiently responsive block switching logic. If the encoder fails to switch to a `SHORT_WINDOW` quickly enough during a transient, the quantization noise is spread across the entire long window, becoming audible before the actual sound starts.
*   **Historical Context:** FAAC's transient detection heuristic has been criticized for being "too conservative," often staying in long blocks when a short block is required to mask noise.

### C. Stereo Image Instability
*   **Symptom:** The stereo field seems to "drift" or "wobble" in complex soundstages.
*   **Root Cause:** Improper Mid/Side (M/S) or Intensity Stereo (IS) decision-making. Aggressive IS usage at low bitrates can destroy spatial cues.
*   **Historical Context:** FAAC's M/S switching logic is often binary and lacks the sophisticated "stereo mask" found in modern encoders.

---

## 2. Modern AAC-LC Architectural "Secrets"

Analysis of modern high-performance encoders (FDK-AAC, Apple AAC) identifies several key logic patterns that provide high quality gains with minimal code complexity.

### A. Adaptive Quantization Rounding (AQR)
*   **Secret:** Instead of a static magic number (0.4054), modern encoders use tonality-aware rounding. By reducing the rounding bias (e.g., to ~0.30) for high-frequency or noisy bands, the encoder can preserve more detail without increasing the bit budget.
*   **FAAC Status:** Recently implemented AQR in `libfaac/quantize.c`.

### B. Refined ATH Scaling
*   **Secret:** The Absolute Threshold of Hearing (ATH) should not be a static curve. Modern encoders scale the ATH based on the target bitrate. At 40 kbps (VSS), the ATH is "lifted" more aggressively in the high frequencies to prevent the encoder from wasting bits on inaudible details, allowing it to focus on the 2kHz-8kHz range where human hearing is most sensitive.

### C. Scalefactor Capping
*   **Secret:** Preventing extreme jumps in scalefactors (the "quantization step") between adjacent bands. Large jumps create "spectral holes" that the ear perceives as metallic artifacts. Capping the delta between scalefactors ensures a smoother spectral envelope.

### D. Responsive TNS (Temporal Noise Shaping)
*   **Secret:** Effective TNS can mask pre-echo even when block switching is suboptimal. Modern encoders use TNS aggressively on both long and short windows to "shape" quantization noise into the temporal envelope of the signal.

---

## 3. The "Golden Triangle" Strategy for 40 kbps / 16kHz

To solve the metallic ringing at 40 kbps/16kHz (VSS) with the smallest possible LoC increase, the following strategy is recommended:

1.  **Refined ATH Scaling:** Implement a bitrate-aware ATH adjustment in `bmask`. This is a ~5-10 line change that provides significant MOS lift by focusing bits on the most critical frequency ranges.
2.  **Scalefactor Capping:** Implement a simple cap on scalefactor deltas in `qlevel`. This prevents the "metallic" artifacts caused by extreme quantization deltas.
3.  **Transient Detection Tuning:** Lower the energy change threshold in `PsyCheckShort` for low-bitrate scenarios to trigger short windows more reliably.

---

## 4. Deliverable Summary: Ratio (MOS Gain / LoC)

| Change | Est. MOS Gain | Est. LoC | Ratio | Priority |
| :--- | :--- | :--- | :--- | :--- |
| ATH Scaling Refinement | +0.15 | 8 | 0.018 | **Tier 1** |
| Scalefactor Capping | +0.10 | 6 | 0.016 | **Tier 1** |
| Transient Detection Tune | +0.08 | 4 | 0.020 | **Tier 1** |
| TNS Expansion (Short) | +0.12 | 25 | 0.004 | **Tier 2** |
| Bit Reservoir Control | +0.25 | 150+ | 0.001 | **Tier 3** |
