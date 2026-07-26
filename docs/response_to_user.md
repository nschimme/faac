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
* **By default (`AUTO` mode), FAAC switches to HE-AAC only for low-quality targets.**
* When using quality-based VBR, the encoder limits HE-AAC selection to `-q 60` or below. If you choose any value higher than `-q 60`, FAAC automatically switches to standard LC-AAC, which offers much better fidelity at higher bitrates.
* If you force HE-AAC at `-q 60`, you get a heavily quantized ~33 kbps stream, which sounds "meh."

---

### Our Recommendation: Stick with the Default `AUTO` Mode

To make things easy, **we strongly recommend relying on FAAC's default `AUTO` mode** rather than manually forcing object types. The encoder is designed to automatically select the best tool for your target:

* **For maximum quality (VBR testing):** Just use `-q 100` (or higher) with no other flags. FAAC will automatically select LC-AAC, preserve full frequency bandwidth, and provide excellent transparency.
  ```bash
  faac -q 100 -o output.m4a input.wav
  ```
* **For low-bitrate targets:** Just specify your target bitrate using `-b` (e.g., `-b 48` or `-b 64`). FAAC will automatically select the highly efficient HE-AAC profile and manage the bit distribution for you.
  ```bash
  faac -b 64 -o output.m4a input.wav
  ```

Letting the default `AUTO` mode handle the decision-making ensures you always get the optimal balance of bandwidth and quality for your target bit budget!

Best regards,
The FAAC Development Team
