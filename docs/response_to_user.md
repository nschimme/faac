Hello AiZ,

Thank you for the detailed feedback and the foobar2000 ABX reports! Achieving 10/10 on both *Enola* and *Big Fun* is impressive and shows you have great ears.

Here are the direct answers to your observations:

### 1. Why `--tns` and `--no-tns` produce bit-identical files at `-b 56`
* **Under 32 kbps per channel, TNS is automatically disabled.**
* When encoding a stereo file with `-b 56`, each channel gets around 28 kbps. To protect the limited bit budget and avoid wasting bits on TNS coefficient overhead, FAAC dynamically disables TNS below 32 kbps/channel.
* Consequently, both `--tns` and `--no-tns` bypass TNS and produce identical files at this bitrate. You will see different files if you encode at `-b 96` or higher.

### 2. Why `-q` never sets bandwidth, but `-b` does
* **`-q` (VBR) preserves the full spectrum, while `-b` (ABR) uses a low-pass filter to fit a budget.**
* **Quality Mode (`-q`):** FAAC retains the entire frequency range up to the Nyquist limit, relying on the quantizer and psychoacoustic model to discard inaudible data.
* **Bitrate Mode (`-b`):** FAAC applies a hard low-pass filter (bandwidth limit) to squeeze the audio into your strict target bitrate without introducing heavy warbling artifacts.

### 3. Why HE-AAC with `-q` maxes out at `-q 60` (~33 kbps)
* **By default (`AUTO` mode), FAAC only uses HE-AAC for low-quality targets.**
* To protect quality, FAAC restricts HE-AAC to `-q 60` or below. Any value above `-q 60` automatically switches the encoder to AAC-LC, which offers much better fidelity at higher bitrates.
* If you force HE-AAC at `-q 60`, you get a heavily quantized ~33 kbps stream, which sounds "meh."

---

### Recommended Settings

* **For maximum quality (VBR):** Use standard AAC-LC with `-q 100` (no bandwidth limit, full transparency).
* **For low-bitrate HE-AAC:** Specify your target bitrate with `-b` instead of `-q` (e.g., `-b 64 --object-type he-aac-v1`) to let the rate controller optimize SBR bit distribution.

Best regards,
The FAAC Development Team
