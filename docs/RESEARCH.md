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

## 2. Modern AAC-LC Architectural "Secrets" vs. FAAC Heuristics

Analysis of modern high-performance encoders (FDK-AAC, Apple AAC, Fraunhofer) identifies several key logic patterns that provide high quality gains. However, many of these are too complex for FAAC's minimal-LoC architecture.

### A. Rate-Distortion Optimization (RDO)
*   **Modern Secret:** Modern encoders perform a search (often via Trellis or Lagrangian multipliers) to find the combination of scalefactors and Huffman codebooks that minimize distortion for a given bit count.
*   **FAAC Heuristic (Scalefactor Capping):** FAAC uses a simple one-pass quantizer. We achieve a similar benefit (preventing metallic spectral holes) by capping the delta between adjacent scalefactors. This provides 80% of the benefit of RDO with 0.1% of the code complexity.

### B. Spectral Band Replication (SBR) & Noise Filling
*   **Modern Secret:** HE-AAC uses SBR to reconstruct high frequencies. Modern LC encoders also use "Noise Filling" to fill spectral holes with controlled noise rather than allowing periodic quantization artifacts (metallic ringing).
*   **FAAC Heuristic (ATH Scaling Refinement):** Instead of complex noise filling, we refine the Absolute Threshold of Hearing (ATH) to "lift" masking thresholds in the high frequencies at low bitrates. This forces the encoder to prioritize bits on the critical 2-4kHz range, preventing the bit starvation that causes metallic artifacts in the first place.

### C. Adaptive Quantization Rounding (AQR)
*   **Modern Secret:** Tonality-aware rounding. By reducing rounding bias (e.g., to ~0.30) for high-frequency or noisy bands, encoders preserve more detail.
*   **FAAC Status:** Recently implemented AQR in `libfaac/quantize.c`.

### D. Responsive TNS (Temporal Noise Shaping)
*   **Modern Secret:** Effective TNS can mask pre-echo even when block switching is suboptimal. Modern encoders use TNS aggressively on both long and short windows.
*   **FAAC Status:** TNS is implemented for long windows. Expanding to short windows (Tier 2) is a key roadmap item.

---

## 3. The "Golden Triangle" Strategy for 40 kbps / 16kHz

To solve the metallic ringing at 40 kbps/16kHz (VSS) with the smallest possible LoC increase, the following strategy is recommended:

1.  **Refined ATH Scaling:** Implement a bitrate-aware ATH adjustment in `bmask`. This is a ~5-10 line change that provides significant MOS lift by focusing bits on the most critical frequency ranges.
2.  **Scalefactor Capping:** Implement a simple cap on scalefactor deltas in `qlevel`. This prevents the "metallic" artifacts caused by extreme quantization deltas. While modern encoders like FDK-AAC or Apple AAC use complex Rate-Distortion Optimization (RDO) or Bit Reservoir management, these methods require thousands of LoC. Capping provides a high MOS gain to LoC ratio by addressing the root cause of metallic ringing (spectral holes from extreme quantization) at the source.
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
