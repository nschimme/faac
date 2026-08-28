# FAAC VBR (-q) Codebase Discovery & Data Flow Analysis Report

## Executive Summary
This document provides a comprehensive technical trace and impact analysis of the Variable Bitrate (VBR) quality parameter (`-q` / `quantqual`) in FAAC, followed by a proposed decoupling architecture.

Currently, FAAC ties VBR quality directly to a linear scale (`VBR_QUAL_BITRATE_SCALE = 1280` bps per quality point) and uses a hardcoded quality threshold (`q <= 75`) to decide between HE-AAC and LC-AAC in `AUTO` mode. Tying `quantqual` to both profile selection and internal perceptual quantizer scaling creates a bitrate/quality gap between 42 kbps and 70 kbps and leads to suboptimal allocation on LC-AAC at mid bitrates.

---

## Step 1: Data Flow Trace of the VBR Quality Parameter

### 1. CLI to Configuration Struct
1. **Command Line Parsing (`frontend/main.c` & `frontend/encode_engine.c`):**
   - The user specifies quality via `-q <val>` (e.g., `-q 75`).
   - `parse_quality_or_bitrate()` in `frontend/encode_engine.c` sets `opts->quant_quality = val` and `opts->bit_rate = 0`.
   - `run_encoding_session_ext()` populates `faac_params.quant_quality = opts->quant_quality` and `faac_params.bit_rate = 0`.
2. **Library Entry Point (`libfaac/faac.c`):**
   - `faac_encoder_open()` copies `p->quant_quality` into `faacEncConfiguration.quantqual`.
   - Calls `faacEncApplyConfig(hEncoder, cfg)`.

### 2. Initialization & Profile Selection (`libfaac/frame.c`)
In `faacEncApplyConfig()`:
1. **AUTO Object Type Crossover:**
   - Evaluates `rate_ok = (config->quantqual <= HE_VBR_QUANTQUAL_MAX)` where `HE_VBR_QUANTQUAL_MAX = (2 * HE_MAX_BITRATE_PER_CH / VBR_QUAL_BITRATE_SCALE) = (2 * 48000 / 1280) = 75`.
   - If `config->quantqual <= 75`, `AUTO` mode resolves to `HE_V1` (HE-AAC).
   - If `config->quantqual > 75`, `AUTO` mode resolves to `LOW` (AAC-LC).
2. **Bandwidth Calculation:**
   - In VBR mode (`config->bitRate == 0`), `CalcBandwidth(0, sampleRate)` returns Nyquist (`sampleRate / 2`), unless `config->bandWidth` was explicitly provided.
3. **Quantizer Configuration:**
   - `hEncoder->config.quantqual = config->quantqual`.
   - Clamped to `[MINQUAL, maxqual]` (where `MINQUAL = 10` and `maxqual = 100` for raw or `100` for ADTS).
   - Assigned to `hEncoder->aacquantCfg.quality = config->quantqual`.
4. **SBR Bitrate Derivation (HE-AAC):**
   - If `aacObjectType == HE_V1`, `sbr_bitrate = config->bitRate ? ... : QuantQualToBitRate(config->quantqual)`.
   - `QuantQualToBitRate(q)` computes `(unsigned long)(q * 1280)`.
   - Passed to `SbrContextUpdateConfig()` to select SBR envelope and noise quantizer resolution (`bs_amp_res`, `dk`).

### 3. Execution & The Rate Controller (`libfaac/frame.c` & `libfaac/quantize.c`)
During `faacEncEncode()`:
1. **Perceptual Target Scaling (`libfaac/quantize.c`):**
   - In `BlocQuant()`, `derive_masking_targets()` calculates masking thresholds scaled by `(float)aacquantCfg->quality / DEFQUAL` (where `DEFQUAL = 100`).
   - Higher quality values increase masking thresholds and decrease quantizer gain step sizes, increasing coded spectral precision and output bitrate.
2. **VBR vs ABR Feedback Loop (`libfaac/frame.c`):**
   - In ABR mode (`hEncoder->config.bitRate > 0`), a per-frame feedback loop dynamically adjusts `aacquantCfg.quality` frame-by-frame based on the bit reservoir and target frame bits.
   - In VBR mode (`hEncoder->config.bitRate == 0`), this feedback loop is completely bypassed. `aacquantCfg.quality` remains fixed at `config->quantqual` throughout the encoding session.

---

## Step 2: Impact Analysis

### 1. What Relies on `VBR_QUAL_BITRATE_SCALE` (1280)?
- **`HE_VBR_QUANTQUAL_MAX` (75):** Hardcodes the `AUTO` profile transition threshold between HE-AAC and LC-AAC in VBR mode.
- **`QuantQualToBitRate()`:** Derives effective whole-stream bitrate for SBR parameter initialization in HE-AAC VBR mode.
- **`BitRateToQuantQual()`:** Derives initial `quantqual` from target bitrate in ABR mode.

### 2. Impact of Passing Different `quantqual` / Target Bitrates
- **Math Integrity in `BlocQuant`:** Scaling `aacquantCfg.quality` alters the perceptual masking targets in `derive_masking_targets()` linearly. The mathematical operations in `quantize.c` (`powf`, log scale factor steps, Huffman book selection) do not break when `quality` changes, provided `quality >= MINQUAL` (10).
- **High-Frequency Bandwidth Issue on LC-AAC:** When operating LC-AAC at lower target bitrates (e.g. 48–64 kbps stereo), setting a low quality value without restricting bandwidth causes LC-AAC to attempt to code spectral lines all the way up to Nyquist (~22.05 kHz). This causes severe bit starvation in lower, acoustically critical frequency bands.
- **Conclusion:** To allow LC-AAC to operate smoothly at lower bitrates, VBR mode must pair quality scaling with an effective target bandwidth derived from the target bitrate curve.

---

## Step 3: Decoupling Strategy Proposal

### 1. User VBR Quality Scale & Continuous Target Bitrate Curve
Maintain user-facing quality range `q` in `[10, 100]` matching `MINQUAL` to `DEFQUAL`. Map `q` to an effective target whole-stream bitrate $B_{\text{target}}(q)$ via a non-linear curve:

$$B_{\text{target}}(q) = B_{\text{min}} \cdot \left( \frac{B_{\text{max}}}{B_{\text{min}}} \right)^{\frac{q - 10}{90}}$$

Where:
- $B_{\text{min}} = 16000$ bps (16 kbps stereo) at $q = 10$.
- $B_{\text{mid}} = 64000$ bps (64 kbps stereo) at $q = 50$.
- $B_{\text{crossover}} = 96000$ bps (96 kbps stereo) at $q = 75$.
- $B_{\text{max}} = 192000$ bps (192 kbps stereo) at $q = 100$.

### 2. AUTO Profile Crossover Trigger
Decouple `AUTO` profile switching from hardcoded `q <= 75`. Instead, derive effective per-channel bitrate:

$$\text{rate\_per\_ch} = \frac{B_{\text{target}}(q)}{\text{numChannels}}$$

- If $\text{rate\_per\_ch} \le 48000$ bps/ch (up to 96 kbps stereo) AND $\text{sampleRate} \ge 32000$ Hz, `AUTO` mode selects **HE-AAC (`HE_V1`)**.
- If $\text{rate\_per\_ch} > 48000$ bps/ch, `AUTO` mode selects **AAC-LC (`LOW`)**.

### 3. Internal Parameter Translation
In `faacEncApplyConfig()`:
1. Compute $B_{\text{target}}(q)$ from $q = \text{config->quantqual}$.
2. Compute per-channel bitrate `rate_per_ch = B_target / numChannels`.
3. Compute effective bandwidth cutoff: `config->bandWidth = CalcBandwidth(rate_per_ch, sampleRate)`.
4. Derive internal quantizer quality `aacquantCfg.quality` from $B_{\text{target}}(q)$.
5. For HE-AAC, pass $B_{\text{target}}(q)$ directly to `SbrContextUpdateConfig()`.

### 4. Backwards Compatibility
All external callers passing `faac_params` or `faacEncConfiguration` will retain full C ABI/API compatibility. All non-linear mapping and parameter translation will take place internally inside `libfaac/frame.c`.
