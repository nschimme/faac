# Comparative AAC Encoder Algorithmic Opportunities Analysis

This document provides a comprehensive analysis of perceptual quality (MOS) benchmark scenario performance across target bitrates and outlines key algorithmic opportunities for improving encoding efficiency, psychoacoustic masking, rate control, and high-efficiency extensions.

All opportunities are described using generic, industry-standard algorithmic terminology and are ranked by expected perceptual impact balanced by implementation difficulty.

---

## 1. Scenario Benchmark Analysis

### Per-Scenario Quality (MOS) Comparison Matrix

| Scenario | Target Benchmark (MOS) | Baseline FAAC (MOS) | Quality Gap (MOS) | Primary Quality Driver / Architectural Focus |
| :--- | :---: | :---: | :---: | :--- |
| **16k_mono_16k** | 3.895 | **4.435** | +0.540 | HE-AAC v1 core spectral coding and low-rate bandwidth scaling. |
| **16k_mono_40k** | **4.924** | 4.916 | -0.008 | Near-transparent LC mono performance; minor bitrate targeting precision. |
| **48k_stereo_24k** | **3.261** | 2.577 | -0.684 | Parametric Stereo (HE-AAC v2) downmixing and SBR envelope resolution. |
| **48k_stereo_32k** | **3.822** | 3.097 | -0.725 | HE-AAC v1 spectral noise floor estimation and M/S stereo energy distribution. |
| **48k_stereo_40k** | **4.115** | 3.503 | -0.612 | HE-AAC v1 adaptive time-frequency grid density and envelope bit allocation. |
| **48k_stereo_48k** | **4.255** | 3.778 | -0.477 | Boundary transition between High-Efficiency and Low-Complexity object types. |
| **48k_stereo_56k** | **4.422** | 3.955 | -0.467 | AAC-LC Huffman sectioning efficiency and M/S stereo decisions. |
| **48k_stereo_64k** | **4.520** | 4.091 | -0.429 | AAC-LC Rate-Distortion optimization and bit reservoir management. |
| **48k_stereo_96k** | **4.731** | 4.358 | -0.373 | AAC-LC scale factor quantization, psychoacoustic masking, and M/S transform. |
| **48k_stereo_128k**| **4.862** | 4.722 | -0.140 | Fine scale factor band bit distribution and bit reservoir buffering. |
| **48k_stereo_160k**| **4.927** | 4.823 | -0.104 | High-fidelity spectral quantization ceiling. |
| **48k_stereo_192k**| **4.961** | 4.884 | -0.077 | Transparency plateau and scale factor delta coding. |
| **48k_stereo_256k**| **4.982** | 4.952 | -0.030 | Full transparency plateau. |

---

## 2. Ranked Algorithmic Opportunities (Impact vs. Difficulty)

The opportunities below are prioritized based on perceptual quality headroom gained per unit of implementation complexity and risk.

---

### Opportunity 1: Dynamic Programming Rate-Distortion Codebook & Section Optimization
* **Impact**: **High** | **Difficulty**: **Medium**
* **Algorithmic Description**:
  Standard AAC syntax encodes spectral coefficients within scale factor bands grouped into contiguous sections, each assigned a specific Huffman codebook (Codebooks 1–11, HCB_ZERO, HCB_PNS). Selecting section boundaries greedy or heuristically often incurs unnecessary section header overhead or suboptimal codebook selection.

  Implementing a formal Rate-Distortion (R-D) optimization using a Viterbi-style dynamic programming trellis search across scale factor bands optimizes the trade-off cost function:
  $$J = \text{Distortion} + \lambda \cdot \text{Rate}$$
  This pathfinder determines the exact global sequence of section splits and codebook assignments that minimizes overall bit consumption for a given target distortion. On mid-to-high stereo bitrates (48k_stereo_56k through 96k), this optimization reclaims significant bit headroom without affecting psychoacoustic masking models.

---

### Opportunity 2: Adaptive Bit Reservoir & Dynamic Frame Rate Control
* **Impact**: **High** | **Difficulty**: **Medium**
* **Algorithmic Description**:
  Audio signals naturally fluctuate in complexity between highly tonal/predictable segments and complex percussive transients. A static per-frame bit allocation forces simple frames to waste capacity while bit-starving transient frames.

  An adaptive bit reservoir mechanism maintains a sliding bit buffer across consecutive frame boundaries. During simple, low-entropy frames, the encoder operates below nominal frame budget to replenish the reservoir. When a transient or complex frame is detected, the encoder draws up to 200% of nominal frame bits from the reservoir. This smooths perceptual quality fluctuations across time, directly closing MOS deficits in transient-heavy content at 48k–96k bitrates.

---

### Opportunity 3: High-Efficiency SBR Envelope Grid Density & Tonality Estimation
* **Impact**: **High** | **Difficulty**: **Medium-High**
* **Algorithmic Description**:
  In High-Efficiency AAC (HE-AAC v1 / SBR), the high-frequency spectrum above crossover is reconstructed using Spectral Band Replication (SBR) envelope data and noise floors. Fixed SBR envelope time-frequency grids fail to match fast temporal attacks or sharp spectral tones.

  Enhancing SBR envelope grid generation involves:
  1. **Adaptive Transient Grid Splitting**: Dynamically increasing temporal envelope resolution (`tEnv`) around transient attacks to prevent pre-echo and temporal smearing.
  2. **Spectral Flatness (Tonality) Indexing**: Calculating per-band spectral flatness measure (SFM) to derive adaptive inverse filtering (`invf`) modes and precise noise floor ratios.

  This improvement targets the large quality gap observed in low-to-mid stereo bitrates (24k–40k).

---

### Opportunity 4: Joint Channel Pair Window Grouping & Transient Synchronization
* **Impact**: **Medium-High** | **Difficulty**: **Medium-High**
* **Algorithmic Description**:
  Channel Pair Elements (CPE) require both channels to share identical window sequence types (e.g., ONLY_LONG_WINDOW vs. EIGHT_SHORT_WINDOW) and window grouping structures to enable joint stereo (M/S) coding across those bands. Independent per-channel window decisions force fallback to non-joint stereo whenever transient detectors trigger asynchronously on left or right channels.

  Implementing joint CPE window selection synchronizes window switching across stereo channel pairs, evaluating combined channel transient energy before committing to short window transitions. This maximizes the proportion of scale factor bands eligible for joint stereo coding, conserving bits across complex stereo spatial fields.

---

### Opportunity 5: Perceptual Noise Substitution (PNS) Energy Estimator & Threshold Calibration
* **Impact**: **Medium** | **Difficulty**: **Low-Medium**
* **Algorithmic Description**:
  Perceptual Noise Substitution (PNS) replaces noisy, noise-like high-frequency scale factor bands with a single noise energy parameter, freeing significant bit budget for lower tonal bands.

  Calibrating PNS selection requires estimating band noise-likeness via local spectral cross-correlation and tonality factors. Adjusting the energy estimation formula ensures that substituted noise scale factors accurately preserve perceived band energy without introducing unnatural noise modulation artifacts.

---

### Opportunity 6: Per-Band Temporal Noise Shaping (TNS) Filter Order & Energy Selection
* **Impact**: **Medium** | **Difficulty**: **Medium**
* **Algorithmic Description**:
  Temporal Noise Shaping (TNS) applies linear prediction along the frequency axis to shape quantization noise in the time domain, which is essential for speech and percussive transients.

  Refining TNS analysis involves adaptive prediction filter order selection and energy threshold gating. By screening spectral envelope variance prior to autocorrelation and Levinson-Durbin recursion, TNS filtering is selectively activated only when temporal envelope fluctuations exceed masking thresholds, avoiding filter transmission overhead on stationary tonal signals.

---

### Opportunity 7: Perceptual Band Weighting & Psychoacoustic Masking Threshold Tuning
* **Impact**: **Medium** | **Difficulty**: **Medium**
* **Algorithmic Description**:
  The psychoacoustic model computes Energy Masking Thresholds (EMT) for each scale factor band using spreading functions and absolute thresholds of hearing.

  Optimizing perceptual band weighting involves refining the spreading function matrix and tuning tonality-dependent masking offsets. Fine-tuning mask calculation prevents over-quantization in sensitive mid-frequency bands (1 kHz to 4 kHz) where human auditory sensitivity peaks, improving overall subjective listening scores.

---

### Opportunity 8: Integrated Parametric Stereo (PS) for Low-Bitrate HE-AAC v2
* **Impact**: **High** | **Difficulty**: **High**
* **Algorithmic Description**:
  For extreme low-bitrate stereo scenarios (e.g., 48k_stereo_24k), full stereo core encoding imposes severe quantization distortion on both channels. Parametric Stereo (HE-AAC v2) solves this by downmixing stereo audio to a single mono core channel while encoding spatial pan and inter-channel coherence parameters in a compact side-channel stream.

  Integrating full Parametric Stereo metadata extraction and bitstream formatting significantly elevates perceptual quality scores at bitrates of 24 kbps and below.
