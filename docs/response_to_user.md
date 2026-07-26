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
* **Under default (`AUTO`) mode, FAAC only resolves to HE-AAC for `-q 60` and below.**
* If you do not specify an object type, the default `AUTO` mode decides between AAC-LC and HE-AAC automatically. To ensure optimal audio quality, the encoder limits HE-AAC selection to `-q 60` or below (yielding around 33 kbps). If you choose any value higher than `-q 60`, `AUTO` automatically switches to standard AAC-LC, which offers much better fidelity at higher bitrates.
* **Can you go higher?** Yes, if you explicitly force HE-AAC using `--object-type he-aac-v1`, you can use higher `-q` settings. However, doing so is discouraged. HE-AAC's Spectral Band Replication (SBR) parametrically reconstructs high frequencies, which works great at low bitrates but is physically incapable of achieving true transparency at higher bitrates, where standard AAC-LC is far superior.

---

### Recommended Settings

* **For maximum quality (VBR):** Use standard AAC-LC with `-q 100` (no bandwidth limit, full transparency).
* **For low-bitrate HE-AAC:** Specify your target bitrate with `-b` instead of `-q` (e.g., `-b 64 --object-type he-aac-v1`) to let the rate controller optimize SBR bit distribution.

Best regards,
The FAAC Development Team
