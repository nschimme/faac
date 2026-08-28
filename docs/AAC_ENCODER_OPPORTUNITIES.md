# AAC Encoder Opportunities and Architectural Analysis

## Joint Stereo (Mid/Side) Processing in FAAC

### Overview
FAAC uses a dominant-component Mid/Side (M/S) joint stereo transform ($M = 0.5(L+R), S = 0$) when side energy is below the mid masking threshold ($side \le em \cdot thrmid$). Comparative analysis against dual-loop encoders (such as Fraunhofer FDK AAC and Apple CoreAudio `qaac`) highlights key architectural trade-offs:

### Dual-Loop vs. Single-Pass Quantization
1. **Dual-Loop Quantizers (FDK AAC / CoreAudio):**
   - Encoders like FDK AAC employ iterative dual-loop quantizers (an outer distortion loop and an inner rate control loop).
   - They can perform non-destructive M/S transforms ($M = 0.5(L+R), S = 0.5(L-R)$) because the inner loop dynamically redistributes bit allocation between $M$ and $S$ channels across iterations.

2. **Single-Pass Quantizer (FAAC `BlocQuant`):**
   - FAAC uses a high-throughput, single-pass psychoacoustic model (`derive_masking_targets`) and scalar quantizer (`BlocQuant`).
   - Applying non-destructive M/S ($S = 0.5(L-R)$) without energy suppression leaves non-zero spectral coefficients in both $M$ and $S$ channels.
   - Because `BlocQuant` quantizes each band independently without an outer bit-redistribution loop, non-zero $S$ coefficients require dedicated scalefactors and Huffman codebooks. Under constrained bit budgets (24k–128k stereo), this causes severe bit starvation across $M$ bands and drops Zimtohrli MOS perceptual quality scores by -0.26 to -0.39 MOS.

3. **Dominant-Component M/S Energy Suppression:**
   - Zeroing $S$ ($S = 0$) when mid energy dominates eliminates scalefactor and codebook bit overhead for the side channel.
   - 100% of the channel pair's bit budget is dedicated to high-precision quantization of $M$.
   - Combined with synchronized short-window grouping (`BlocGroupCPE`), this delivers optimal perceptual MOS scores and prevents cross-channel quantization pre-echo on transient blocks at low bitrates ($\le 48\text{ kbps/ch}$).
