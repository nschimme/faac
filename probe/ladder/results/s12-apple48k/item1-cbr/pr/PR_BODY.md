## ratecontrol: aim CBR frames at the reservoir midpoint

Under CBR the reservoir floor already forces the nominal rate, so the ABR aim of 0.99 x budget only turns into fill:
1-1.4 % of the stream was stuffing at 192/320 kbps. CBR now aims each frame at the budget plus the reservoir's
distance from half full (amortized like the balance), which spends that surplus as signal and keeps the buffer clear
of both the stuffing floor and the cap. ABR and VBR are unchanged.

**Measured (local, 48 kHz stereo LC CBR 128-320, 77 clips of faac-benchmark 9f5f102; Zimtohrli via the benchmark's scorer; realized bitrate):**
- BD-rate vs master: -0.72 % [-0.88, -0.54] (FAAD2), -0.73 % (FFmpeg), -0.75 % (FDK decoder), linear interpolation, clip bootstrap; PCHIP -0.69..-0.72 %. 70/77 clips better.
- Worst clip x rung vs master: -0.0095 (fms, 160k); no cell below -0.01.
- vs Apple AAC (packaged afconvert CBR streams): BD-rate -1.6..-1.8 %, CI excludes 0 (master CBR: unresolved).
- Realized rate +0.00..+0.12 %; stuffing 0.97 % -> 0.32 % at 192k; cap retries 9.5 % -> 4.9 %; no reservoir underflow.
- callgrind, 10 s at 192k CBR: velvet -50 % (fewer cap retries), Bachpsichord +0.3 %.
- Bit-exact: ABR/VBR payload identical to master (18/18 LC/HE encodes).
- Footprint (gcc 13 LTO, .text+.rodata+.data): +208 B.

**Limits:** only 48 kHz LC CBR was scored locally; HE-v1/HE-v2, 44.1/32 kHz and mono speech CBR rely on the CI
benchmark. The constant-free rule (half capacity, existing amortization) was not tuned.
