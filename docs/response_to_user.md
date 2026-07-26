Hello AiZ,

Thanks for the detailed feedback and the foobar2000 ABX reports! Achieving 10/10 on both *Enola* and *Big Fun* is impressive and shows you have excellent ears.

To align expectations: FAAC is designed for low footprint, high throughput, and embedded/streaming use cases rather than absolute archival/audiophile transparency. Easily passing an ABX against a mature encoder like FDK-AAC is completely expected, as FAAC focuses on speed and portability over ultimate quality.

Here are the direct technical answers to your observations:

### 1. Why `--tns` and `--no-tns` produce bit-identical files at `-b 56`
* **TNS is currently WIP and disabled by default.** Our TNS implementation is still a work-in-progress, and it is kept off by default because the block switcher serves as a superior transient detector as of now.
* **TNS is automatically bypassed at low bitrates.** Even if enabled, FAAC dynamically disables TNS below **32 kbps per channel** to save precious bit budget from coefficient overhead.
* At `-b 56` stereo (~28 kbps/ch), both `--tns` and `--no-tns` bypass TNS entirely, resulting in bit-identical files. You will see different files if you test at higher bitrates (e.g. `-b 96` or above).

### 2. Why `-q` never sets bandwidth, but `-b` does
* **`-q` (VBR) preserves the full spectrum, while `-b` (ABR/CBR) uses low-pass filtering to meet a strict budget.**
* **VBR Mode (`-q`):** FAAC retains the entire frequency spectrum up to the Nyquist limit, relying purely on the psychoacoustic model and quantizer to naturally discard inaudible frequencies.
* **Bitrate Mode (`-b`):** FAAC applies a hard low-pass filter (bandwidth cutoff) to fit the audio into your strict target bitrate without introducing heavy warbling or pre-echo.

### 3. Why HE-AAC with `-q` maxes out at `-q 60` (~33 kbps)
* **By default (`AUTO` mode), FAAC switches to HE-AAC only for low-quality targets where SBR is necessary.**
* To protect fidelity, `AUTO` mode limits HE-AAC selection to `-q 60` or below. Any quality setting above `-q 60` automatically switches the encoder to standard LC-AAC, which offers much better fidelity than HE-AAC at higher bit budgets.
* If you force HE-AAC at `-q 60` under `AUTO`, you get a heavily quantized ~33 kbps stream, which will sound "meh."

---

### Recommended Setup

Because FAAC is optimized for footprint and speed, we strongly recommend letting the default **`AUTO` mode** handle the profile choices automatically:

* **For general use:** Just use `-q 100` (or higher) with no other flags. FAAC will automatically select LC-AAC, preserve full frequency bandwidth, and provide a solid compromise of speed and quality.
  ```bash
  faac -q 100 -o output.m4a input.wav
  ```
* **For low-bitrate streaming:** If you want to evaluate HE-AAC, use `-b` to set a targeted bitrate (e.g., `-b 64`). The encoder will automatically engage HE-AAC and manage the bit distribution for SBR.
  ```bash
  faac -b 64 -o output.m4a input.wav
  ```

Let us know if you run any further ABX tests with these settings!

Best regards,
The FAAC Development Team
