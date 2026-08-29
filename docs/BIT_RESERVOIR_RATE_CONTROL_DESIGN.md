# FAAC Adaptive Bit Reservoir & Rate Control Architecture

## Executive Summary
This design document describes the architecture, control mechanics, and parameter levers for FAAC's **Adaptive Bit Reservoir and Dynamic Rate Control System**.

The primary objective is to resolve perceptual quality (Zimtohrli MOS) deficits on complex, polyphonic, and transient-heavy audio content by maintaining an inter-frame bit buffer, while eliminating rate-control hunting, quality modulation, and bitrate bias.

---

## 1. Core Architecture

### 1.1 State Variables (`faacEncStruct`)
In `libfaac/frame.h`, `faacEncStruct` maintains persistent reservoir state:
- `bitReservoir` (`int`): Current bit balance available in the reservoir.
- `bitReservoirCap` (`int`): Maximum capacity of the bit reservoir in bits.

### 1.2 Reservoir Sizing & Initialization (`faacEncApplyConfig`)
For ABR mode (`config.bitRate > 0`), nominal frame bit budget `desbits` is computed as:
$$\text{desbits} = \frac{\text{bitRate} \cdot \text{FRAME\_LEN}}{\text{sampleRate}}$$

The reservoir capacity is capped according to ISO/IEC 14496-3 maximum per-channel bit payload boundaries ($6144 \text{ bits/ch}$) and 200% nominal frame budget:
$$\text{maxReservoirBits} = \max(0, 6144 \cdot \text{numChannels} - \text{desbits})$$
$$\text{bitReservoirCap} = \min(\text{maxReservoirBits}, 2 \cdot \text{desbits})$$
$$\text{bitReservoir}_{\text{init}} = \frac{\text{bitReservoirCap}}{2}$$

In VBR mode (`bitRate == 0`), `bitReservoir` and `bitReservoirCap` are set to `0` and all rate control logic is completely bypassed.

---

## 2. Dynamic Bit Allocation & Reservoir Mechanics

### 2.1 Complex / Over-Budget Frame Draw
When a complex or transient frame exceeds nominal bit budget ($\text{diff} = \text{desbits} - \text{totalBits} < 0$), it draws surplus bits from `bitReservoir` capped at up to 100% extra nominal bits (200% maximum frame budget):
$$\text{maxDraw} = \min(-\text{diff}, \text{desbits})$$
$$\text{absorbed} = \min(\text{maxDraw}, \text{bitReservoir})$$
$$\text{effectiveBits} = \text{totalBits} - \text{absorbed}$$
$$\text{bitReservoir} \leftarrow \text{bitReservoir} - \text{absorbed}$$

### 2.2 Simple Frame Replenishment
When a simple frame operates below nominal budget ($\text{diff} > 0$), unspent bits replenish `bitReservoir`:
$$\text{space} = \text{bitReservoirCap} - \text{bitReservoir}$$
$$\text{deposited} = \min(\text{diff}, \text{space})$$
$$\text{bitReservoir} \leftarrow \text{bitReservoir} + \text{deposited}$$
$$\text{effectiveBits} = \text{totalBits}$$

---

## 3. Reservoir-State Adaptive Damping Control Loop

To prevent quality scale factor hunting and eliminate bitrate undershoot/overshoot bias, feedback loop damping dynamically adapts based on reservoir fill ratio:
$$\text{fillRatio} = \frac{\text{bitReservoir}}{\text{bitReservoirCap}}$$

### Adaptive Damping Profile
- **Near Equilibrium ($0.25 \le \text{fillRatio} \le 0.75$)**: Damping is set to `RC_DAMPING_FACTOR = 0.60f` to ensure smooth, low-pass filtered quality transitions across consecutive frames.
- **Depleted or Surplus Extreme ($\text{fillRatio} < 0.25$ or $\text{fillRatio} > 0.75$)**: Damping accelerates to `0.85f` (un-dampened recovery) so the rate controller quickly adjusts quantization quality before the reservoir runs dry or overflows.

$$\text{fix}_{\text{raw}} = \frac{\text{desbits} - \text{sbrBits}}{\text{effectiveBits} - \text{sbrBits}}$$
$$\text{fix} = (\text{fix}_{\text{raw}} - 1.0f) \cdot \text{damping} + 1.0f$$
$$\text{aacquantCfg.quality} \leftarrow \text{aacquantCfg.quality} \cdot \text{fix}$$

---

## 4. Parameter Levers & Tuning Summary

| Lever Parameter | Description | Optimal Value |
| :--- | :--- | :--- |
| `bitReservoirCap` | Maximum reservoir capacity | $\min(6144 \cdot \text{ch} - \text{desbits}, 2 \cdot \text{desbits})$ |
| `maxDraw` | Maximum burst draw ceiling per frame | $1.0 \cdot \text{desbits}$ (200% total frame budget) |
| `RC_DAMPING_FACTOR` | Nominal control loop damping | $0.60f$ |
| `DAMPING_ACCEL` | Accelerated recovery damping | $0.85f$ |
