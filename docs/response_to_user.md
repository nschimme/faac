Hello AiZ,

Thanks for the detailed feedback and the foobar2000 ABX reports! Achieving 10/10 on both *Enola* and *Big Fun* is an impressive result that shows you have excellent ears for highlighting compression artifacts.

Here are the direct technical reasons behind the behaviors you encountered:

### 1. Why `--tns` and `--no-tns` produce bit-identical files at `-b 56`
* **Under 32 kbps per channel, TNS is automatically disabled.**
* At `-b 56` (stereo), the budget translates to ~28 kbps per channel. To prevent the fixed bitstream overhead of TNS coefficients from starving the core quantization loop on low-bitrate frames, FAAC dynamically disables TNS below 32 kbps/channel.
* As a result, both paths bypass TNS and yield identical output. You will see different files if you test at higher bitrates (e.g. `-b 96` or above).

### 2. Why `-q` never sets bandwidth, but `-b` does
* **`-q` (VBR) preserves the full spectrum, while `-b` (ABR/CBR) uses low-pass filtering to meet a strict budget.**
* **VBR (`-q`):** FAAC retains the entire frequency spectrum up to the Nyquist limit, relying purely on the psychoacoustic model and quantizer to naturally drop inaudible coefficients.
* **ABR/CBR (`-b`):** The encoder applies a dynamic bandwidth cutoff (low-pass filter) to squeeze the audio into your strict target bitrate without introducing heavy warbling or pre-echo.

### 3. Why HE-AAC with `-q` maxes out at `-q 60` (~33 kbps)
* **By default (`AUTO` mode), FAAC switches to HE-AAC only for low-bitrate targets where SBR is necessary.**
* To protect fidelity, `AUTO` mode caps HE-AAC at `-q 60`. Any higher quality target automatically switches to standard AAC-LC, which offers much better fidelity than HE-AAC at higher bit budgets.
* Specifying high VBR qualities like `-q 100` or above with `AUTO` will correctly use LC-AAC and avoid SBR entirely.

---

### Recommended Setup

We strongly recommend letting the default **`AUTO` mode** handle the choices for you, as it optimizes the codec profile based on your target:

* **For maximum fidelity (critical listening/ABX):** Use `-q 100` or higher. This defaults to standard AAC-LC, preserves full frequency bandwidth, and targets true transparency.
  ```bash
  faac -q 100 -o output.m4a input.wav
  ```
* **For low-bitrate tests:** If you want to evaluate HE-AAC, use `-b` to set a targeted bitrate (e.g., `-b 64`). The encoder will automatically engage HE-AAC and optimize bit distribution for SBR.
  ```bash
  faac -b 64 -o output.m4a input.wav
  ```

Let us know if you run any further ABX tests with these settings!

Best regards,
The FAAC Development Team
