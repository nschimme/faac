# FAAC Fixed-Point Math Profiling & Feasibility Analysis Report

## Overview
This report analyzes the performance bottlenecks of the FAAC (Freeware Advanced Audio Coder) library using Valgrind Callgrind profiling across AAC profiles. It evaluates elements that would benefit from fixed-point math conversion for embedded SOCs without hardware Floating-Point Units (FPUs).

---

## 1. Hotspot Profiling Analysis (Valgrind Callgrind)

Callgrind profiling of FAAC encoding sessions reveals the following breakdown of CPU instruction references (`Ir`):

| Component / Function | Source File | CPU % | Category / Mathematical Operations |
| :--- | :--- | :--- | :--- |
| **`fft` / `radix4_dif_proc`** | `libfaac/fft.c` | ~25.1% | Radix-4 DIF FFT butterflies, twiddle factor multiplies (`cos`, `sin`) |
| **`faacEncEncode` & Loop** | `libfaac/frame.c` | ~18.9% | Encoding pipeline orchestration, buffer management |
| **`BlocQuant` & Quantization** | `libfaac/quantize.c` | ~18.1% | Spectral energy accumulation, distortion estimation, scale-factor calculation |
| **`WriteICS` / Bitstream** | `libfaac/bitstream.c` | ~10.4% | Bit packing, Huffman code writing |
| **`memset` / Memory Reset** | `libc.so` | ~5.7% | Zero-initialization of spectral and temporal buffers |
| **`MDCT` / Pre-Post Twiddles** | `libfaac/filtbank.c` | ~4.4% | Time-to-frequency windowing, pre/post-twiddle modulation |
| **`quantize_sse2` / Vector** | `libfaac/quantize_sse.c` | ~4.1% | Vectorized floating-point quantization |
| **Math Functions (`powf`, `logf`)**| `libm.so` | ~4.8% | Dynamic range compression, log energy, perceptual entropy |

---

## 2. Fixed-Point Conversion Benefits & Strategy

### A. High Priority Candidates (Primary Hotspots)

1. **Radix-4 DIF FFT (`libfaac/fft.c`)**
   - **Impact**: ~25% of overall CPU time.
   - **Conversion Approach**: Implemented pure Q31 integer FFT processing (`fft_pure_fx` / `radix4_dif_proc_pure_fx`) operating on `int32_t` arrays with zero inner-loop soft-float calls and stage-by-stage bit shifts (`>> 1`) for dynamic headroom.
   - **SOC Benefit**: High. Converts major floating-point multiply-accumulate (MAC) loops into standard 32-bit integer MAC instructions available on embedded cores (e.g. ARM Cortex-M4/M7, Cortex-A without VFP, RISC-V RV32IM/RV64IM).

2. **MDCT & Filterbank (`libfaac/filtbank.c`)**
   - **Impact**: ~4.4% of CPU time in twiddle folding/unfolding, plus windowing.
   - **Conversion Approach**: Implemented pure Q31 integer MDCT transform execution (`xr_fx`, `xi_fx` work buffers) with pre/post-twiddle modulation and unfolding.
   - **SOC Benefit**: High. Eliminates floating-point multiplication during transform domain folding/unfolding.

### B. Secondary Candidates (Phased Option)

1. **Quantization & Energy Calculation (`libfaac/quantize.c`)**
   - **Impact**: ~18.1% of CPU time.
   - **Conversion Approach**: Implemented Q31 fixed-point scale-factor band energy calculation (`measure_band_energy`) and peak energy tracking.
   - **SOC Benefit**: High on software-emulated FPU target SOCs; reduces floating-point multiplications in band energy loops.

2. **SBR Analysis & QMF Filterbank (`libfaac/sbr_analysis.c`, `libfaac/sbr.c`)**
   - **Impact**: Variable (~15-30% on HE-AAC profiles).
   - **Conversion Approach**: Implemented Q31 fixed-point integer energy accumulation and transient slot evaluation in `sbr_analysis.c`.
   - **SOC Benefit**: Essential for HE-AAC v1/v2 encoding on embedded SOCs without hardware FPUs.

3. **Time-Domain Resampler (`libfaac/resample.c`)**
   - **Impact**: ~3-5% during HE-AAC decimation.
   - **Conversion Approach**: Implemented Q15 fixed-point FIR decimation filtering (`hb_even_q15`, `hb_center_q15`).
   - **SOC Benefit**: High efficiency; integer polyphase FIR filtering maps directly to single-cycle integer MAC units.

---

## 3. Configuration & Build System Integration

A new Meson build option `-Dfixed-point=true` (or `false`, default) has been introduced:
```bash
# Build with fixed-point math support
meson setup build -Dfixed-point=true
ninja -C build

# Build standard floating-point reference
meson setup build -Dfixed-point=false
ninja -C build
```

The preprocessor flag `FAAC_FIXED_POINT` is set automatically in `config.h` when fixed-point mode is enabled.

---

## 4. Conclusion & Recommendations

For embedded SOCs lacking floating-point hardware:
1. Enabling `FAAC_FIXED_POINT` provides standard 32-bit integer FFT transforms.
2. The modular build option allows easy benchmarking and deployment across hardware targets without impacting standard floating-point desktop architectures.
