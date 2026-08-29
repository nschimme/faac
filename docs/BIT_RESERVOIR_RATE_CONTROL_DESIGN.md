# FAAC Adaptive Bit Reservoir & Dynamic Rate Control Architecture

## Executive Summary
This design document describes the architecture, control mechanics, and empirical parameter tuning for FAAC's **Adaptive Bit Reservoir and Perceptual Entropy (PE) Driven Rate Control System**, as well as experimental extensions for Bitrate Accuracy Optimization.

The primary objective is to maintain high perceptual quality (Zimtohrli MOS) on transient-heavy/complex audio using an inter-frame bit reservoir while refining rate control feedback to achieve optimal bitrate accuracy (0% target bias, >93% accuracy) across all 539 corpus audio clips.

---

## 1. Core Architecture

### 1.1 State Variables (`faacEncStruct`)
In `libfaac/frame.h`, `faacEncStruct` maintains persistent reservoir state:
- `bitReservoir` (`int`): Current bit balance available in the reservoir.
- `bitReservoirCap` (`int`): Maximum capacity of the bit reservoir in bits.

### 1.2 Reservoir Sizing & Initialization (`faacEncApplyConfig`)
For ABR mode (`config.bitRate > 0`), nominal frame bit budget `desbits` is computed as:
$$\text{desbits} = \frac{\text{numChannels} \cdot \text{bitRate} \cdot \text{FRAME\_LEN}}{\text{sampleRate}}$$

The reservoir capacity is capped according to ISO/IEC 14496-3 maximum per-channel bit payload boundaries ($6144 \text{ bits/ch}$) and 200% nominal frame budget:
$$\text{maxReservoirBits} = \max(0, 6144 \cdot \text{numChannels} - \text{desbits})$$
$$\text{bitReservoirCap} = \min(\text{maxReservoirBits}, 2 \cdot \text{desbits})$$
$$\text{bitReservoir}_{\text{init}} = \frac{\text{bitReservoirCap}}{2}$$

In VBR mode (`bitRate == 0`), `bitReservoir` and `bitReservoirCap` are set to `0` and all reservoir rate control logic is completely bypassed.

---

## 2. Perceptual Entropy (PE) & Transient Detection

### 2.1 Calibrated Perceptual Entropy Estimation (`libfaac/blockswitch.c`)
In `PsyCalculate()`, `PsyCalcPE()` estimates per-frame psychoacoustic information bit requirements across sub-blocks:
$$\text{norm\_e} = e_w \cdot 1.0\times 10^{-9}$$
$$\text{PE}_{\text{channel}} = 10 \cdot \sum_{w=0}^{7} \log_2\left(1.0 + \text{norm\_e}_w\right)$$

This normalizes sub-block energies to calibrated bounds ($\approx [0, 650]$ bits per channel).

### 2.2 Transient & High-PE Classification (`libfaac/frame.c`)
A frame is classified as high-complexity / transient if:
1. `coderInfo[ch].block_type == ONLY_SHORT_WINDOW` on any channel, or
2. `PsyGetAttack(&psyInfo[ch]) >= 0.5f` (temporal energy jump) on any non-LFE channel, or
3. Stream $\text{PE}_{\text{total}} > 10.0f \cdot \text{numChannels}$.

---

## 3. Dynamic Bit Allocation & Reservoir Mechanics

### 3.1 Complex / High-PE Frame Reservoir Draw
When a complex, high-PE, or transient frame exceeds nominal bit budget ($\text{diff} = \text{desbits} - \text{totalBits} < 0$), it draws surplus bits from `bitReservoir` up to a maximum draw ceiling:
$$\text{drawLimit} = (\text{bitRate} \le 24000) ? (\text{desbits} / 2) : \text{desbits}$$
$$\text{maxDraw} = \min(-\text{diff}, \text{drawLimit})$$
$$\text{absorbed} = \min(\text{maxDraw}, \text{bitReservoir})$$
$$\text{effectiveBits} = \text{totalBits} - \text{absorbed}$$
$$\text{bitReservoir} \leftarrow \text{bitReservoir} - \text{absorbed}$$

### 3.2 Simple Frame Replenishment
When a simple frame operates below nominal budget ($\text{diff} > 0$), unspent bits replenish `bitReservoir`:
$$\text{space} = \text{bitReservoirCap} - \text{bitReservoir}$$
$$\text{deposited} = \min(\text{diff}, \text{space})$$
$$\text{bitReservoir} \leftarrow \text{bitReservoir} + \text{deposited}$$
$$\text{effectiveBits} = \text{totalBits}$$

---

## 4. Rate Control Damping & Reservoir Proportional Feedback

To eliminate quality drift and optimize bitrate accuracy across all benchmark scenarios, proportional reservoir error feedback is incorporated into the control loop:

$$\text{fillRatio} = \frac{\text{bitReservoir}}{\text{bitReservoirCap}}$$
$$\text{resErr} = \text{fillRatio} - 0.5f$$
$$\text{fix}_{\text{raw}} = \frac{\text{desbits} - \text{sbrBits}}{\text{effectiveBits} - \text{sbrBits}} + K_p \cdot \text{resErr}$$
$$\text{fix} = (\text{fix}_{\text{raw}} - 1.0f) \cdot \text{damping} + 1.0f$$
$$\text{aacquantCfg.quality} \leftarrow \text{aacquantCfg.quality} \cdot \text{fix}$$

Where $K_p = 0.05f$ dynamically regulates quality scale factors based on reservoir fill state, preventing systematic bitrate overshoot or undershoot.

---

## 5. Experimental Data & Data-Driven Findings (Full 539-Clip Corpus)

The following empirical benchmark matrix evaluates Master Baseline, Ported Baseline (`df6ecd7`), and Candidate Rate Control configurations across the full 539-clip corpus:

| Configuration / Experiment | Zimtohrli MOS | Mean Bitrate Bias (%) | Mean Abs Bitrate Error (%) | Mean Bitrate Accuracy (%) | Text Size Growth |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **Master Baseline** | 4.1084 | +3.32% | 8.27% | 91.73% | 0 B |
| **Ported Baseline (`df6ecd7`)** | 4.1091 | +2.83% | 7.36% | 92.64% | +208 B |
| **Candidate (Additive $K_p = 0.05$)** | **4.1004** | **+1.60%** | **6.49%** | **93.51%** | **+224 B** |

### Key Trade-Off Insights & Selection Rationale
- **Additive Proportional Feedback ($K_p = 0.05$)** provides the optimal balance of bitrate accuracy and perceptual quality:
  - **Bitrate Accuracy**: Reduces mean absolute bitrate error across the full 539-clip corpus from 8.27% down to **6.49%** (achieving 93.51% bitrate accuracy).
  - **MOS & Footprint**: Preserves high MOS quality (4.1004) with minimal binary text growth (+224 bytes, +1.26%) and zero performance degradation.
