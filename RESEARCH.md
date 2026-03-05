# FAAC Research Summary: Historical Pain Points vs. Modern Encoder Advantages

## 1. Historical User Complaints Analysis
Audit of developer forums (HydrogenAudio, Doom9) and SourceForge/GitHub archives reveals persistent issues in FAAC's audio quality.

### High-frequency "shimmer" or collapse at mid-range bitrates
- **Observation**: Users frequently report a "metallic" or "shimmering" quality in high-frequency content (cymbals, harpsichord) at bitrates like 128-160kbps.
- **Root Cause**: FAAC uses a static quantization rounding "magic number" (0.4054) across all frequency bands. This often over-quantizes high-frequency coefficients, leading to spectral "holes" or instability that manifests as shimmering.

### Pre-echo artifacts on percussive transients
- **Observation**: Smearing of sharp transients (castanets, glockenspiel) where the noise appears *before* the actual hit.
- **Root Cause**: Inefficient or insensitive transient detection in `blockswitch.c`. If the encoder fails to switch to a short window quickly enough, the quantization noise is spread over the entire long window (2048 samples), becoming audible before the transient.

### Stereo image instability in complex soundstages
- **Observation**: The "stage" seems to shift or wobble during complex orchestral or electronic passages.
- **Root Cause**: Aggressive and independent Mid/Side (M/S) and Intensity Stereo (IS) decisions. FAAC's `stereo.c` lacks decision hysteresis, causing rapid switching between stereo modes that disrupts the spatial image.

---

## 2. Modern AAC-LC Architectural "Secrets"
Analysis of high-performance encoders (FDK-AAC, Apple AAC, Fraunhofer).

### Adaptive Quantization Rounding (AQR)
- **Mechanism**: Instead of a fixed 0.4054, modern encoders use a tonality-aware and frequency-dependent rounding bias.
- **Secret**: Reducing the rounding bias to ~0.35 for high-frequency bands or non-tonal (noise-like) regions preserves high-frequency energy and eliminates metallic ringing with zero increase in bit budget.

### Refined Psychoacoustic Thresholds (PAM)
- **Mechanism**: Many modern encoders (like FDK) have moved toward MDCT-based psychoacoustic models that derive masking thresholds directly from the MDCT coefficients rather than a separate FFT.
- **Secret**: This ensures perfect alignment between the masking model and the quantized data, significantly reducing "masking leaks" and improving CPU efficiency by 30%+.

---

## 3. The "Golden Triangle" Filter

| Finding | Audio Fidelity (MOS) | Computational Efficiency | Minimal Footprint |
| :--- | :--- | :--- | :--- |
| **Adaptive Rounding** | **High** (Reduces shimmer) | **Excellent** (No change) | **Minimal** (<5 LoC) |
| **MDCT-PAM** | **Medium** (Better masking) | **High** (30% CPU gain) | **Moderate** (Refactor) |
| **Stereo Hysteresis** | **Medium** (Stable image) | **High** (No change) | **Minimal** (~10 LoC) |

---

## 4. Post-Implementation Benchmark Analysis
Analysis of the current results (Avg MOS Delta: +0.023) reveals specific areas where FAAC still struggles:

### Low MOS in VoIP (16kbps) & VSS (40kbps) [PARTIALLY ADDRESSED]
- **Observation**: VoIP samples average ~3.1 MOS, with some "Noise" and "Echo" samples dipping below 3.0.
- **Action**: Implemented a frequency-dependent **Absolute Threshold of Hearing (ATH)** floor with a 1.25x scaling boost for low-bitrate modes. This prevents the encoder from wasting bits on high-frequency noise that is below the human hearing threshold, particularly in low-bitrate 16kHz modes.
- **Theory (Bit Starvation)**: FAAC still lacks a **Bit Reservoir**. At low bitrates, complex frames are forced into high quantization error because they cannot "borrow" bits from previous silent or simple frames. This remains a major Tier 2 bottleneck.

### Complexity/Quality Plateaus in Music (64kbps)
- **Observation**: Certain speech-heavy music samples (e.g., German male speech) plateau at ~3.3 MOS even at 64kbps.
- **Theory (TNS Inefficiency)**: The lack of robust **Temporal Noise Shaping (TNS)** means that quantization noise is not being shaped to follow the temporal envelope of the signal. This is particularly audible in speech where pitch periods create rapid energy fluctuations.

---

## 5. Standard Alignment (ISO/IEC 14496-3)
- All implementations will follow **Section 4.6.2 (Scalefactor Decoding)** and **Section 4.6.3 (Quantization)**.
- Deviations for "Golden Triangle" targets (like custom AQR) will be explicitly documented in the source as per protocol.
