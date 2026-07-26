# Technical Response to User Bug Report (AiZ)

Hello AiZ,

Thank you for your detailed feedback and the foobar2000 ABX reports! Achieving 10/10 on both *Enola* and *Big Fun* is a fantastic result that shows you have excellent ears—so don't sell yourself short on your ABXing skills!

We have thoroughly analyzed the behavior of FAAC under the conditions you described and want to explain the technical details of what you observed.

---

### 1. Why `--tns` and `--no-tns` Produce Bit-Identical Files at `-b 56`

You noticed that specifying `--tns` or `--no-tns` had no effect on the final encoded file. This is actually an intentional optimization in FAAC's low-bitrate heuristics:

* **Bitrate per Channel:** When you encode a stereo file with `-b 56` (56 kbps), the encoder allocates approximately half of that budget to each channel, resulting in **28 kbps per channel**.
* **TNS Bit Gating:** Temporal Noise Shaping (TNS) is highly effective at hiding quantization noise under transients. However, transmitting the LPC filter coefficients carries a fixed bitstream overhead. At very low bitrates (below 32 kbps per channel), this overhead is too expensive and would starve the core quantization loop, degrading the overall sound quality of stationary/tonal frames.
* **The Threshold:** To protect the limited bit budget, FAAC dynamically disables TNS below **32 kbps per channel**.
* **Result:** Since 28 kbps/ch is below the 32 kbps threshold, the maximum LPC order is set to 0 and TNS is completely bypassed for both `--tns` and `--no-tns`, yielding bit-identical output files. If you encode at a higher bitrate (e.g., `-b 96` or above for stereo, or `-b 56` for a mono file), you will see that `--tns` and `--no-tns` produce different files.

---

### 2. Why `-q` Never Limits Bandwidth, but `-b` Does

This comes down to the fundamental difference in how Quality-based Variable Bitrate (VBR) and Average Bitrate (ABR/CBR) operate in FAAC:

* **Quality-based VBR (`-q`):** When using `-q`, you are asking the encoder to maintain a consistent audio quality level. FAAC keeps the **entire frequency spectrum up to the Nyquist limit** (`sample_rate / 2` — e.g., 22.05 kHz for 44.1 kHz input). Instead of a hard low-pass filter, it relies on the psychoacoustic model and the quantizer to naturally discard spectral coefficients that fall below the threshold of human hearing.
* **Average Bitrate (`-b`):** When using `-b`, the encoder is constrained by a strict bit budget. To fit the audio into this budget without causing severe compression/warbling artifacts, FAAC applies a **dynamic low-pass filter (bandwidth cutoff)** based on the target bitrate. For low bitrates, it filters out high frequencies to focus all remaining bits on the highly audible lower frequencies.

---

### 3. HE-AAC Capping at `-q 60` (~33 kbps) and Quality Observations

You observed that encoding HE-AAC files with `-q` maxes out at `-q 60` (yielding around 33 kbps), where the quality is "meh." Here is why that happens:

* **Dynamic Object Type Selection (`AUTO`):** By default, FAAC uses the `AUTO` object type. In this mode, the encoder decides whether to use **AAC-LC** (Low Complexity) or **HE-AAC** (High Efficiency, which uses Spectral Band Replication) based on your target quality/bitrate:
  * HE-AAC is designed specifically for low-bitrate efficiency. SBR reconstructs the upper octave of the spectrum parametrically from a 2:1 downsampled core. This works great at low bitrates but naturally plateaus in high-fidelity scenarios.
  * To ensure optimal quality, the `AUTO` selector restricts HE-AAC to `-q` levels of **60 or below**.
  * If you specify a `-q` value **above 60**, `AUTO` automatically switches to **AAC-LC** to preserve full audio fidelity and bandwidth.
* **Why the Quality is "Meh":** If you force HE-AAC at `-q 60` or below, the core is heavily quantized and constrained, leading to a low bitrate (~33 kbps) and audible artifacts.

---

### Recommended Settings for Your Tests

To get the best performance out of FAAC for your ABX testing, we recommend the following guidelines:

1. **For High-Fidelity/Transparency (AAC-LC):**
   Use Quality-based VBR (`-q`) without any bitrate restrictions. This will give you full bandwidth and optimal quality:
   ```bash
   faac -q 100 -o output.m4a input.wav
   ```
   *(Values between `-q 100` and `-q 120` generally offer excellent transparency for stereo tracks).*

2. **For High-Quality Low-Bitrate (HE-AAC):**
   Instead of using `-q` for HE-AAC, use Average Bitrate (`-b`) to allow the rate controller to dynamically distribute the bit budget between the downsampled core and the SBR reconstruction:
   ```bash
   faac -b 64 --object-type he-aac-v1 -o output.m4a input.wav
   ```
   *(For stereo tracks, 48 kbps to 64 kbps is the sweet spot where HE-AAC provides a significant quality boost over standard AAC-LC).*

We hope this clarifies the design of FAAC's rate control and TNS tools! Please let us know if you have any more questions or findings.

Best regards,
The FAAC Development Team
