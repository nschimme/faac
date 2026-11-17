## ratecontrol: spend the CBR reservoir surplus above half full (stereo >= 64 kbps/ch)

Under CBR the reservoir floor already forces the nominal rate, so the 0.99 x budget aim only turns into fill: about
1 % of the stream at 192/320 kbps was stuffing. For stereo CBR at >= 64 kbps/ch the core target now adds back the
1 % and the reservoir's excess over half capacity (amortized like the balance) once the reservoir is past half
full; below half the aim is unchanged. ABR, VBR, mono and stereo below 64 kbps/ch are bit-identical to master.

Replaces the first version, whose midpoint rule also saved bits below half full and at every rate: that starved
bursts at low rates and failed the mos gate (35 CBR cells: mono speech 16k/24k, low-rate stereo). Spending only
above half full fixed most of it, but low-rate mono still swung single clips by +-0.2 MOS with no mean gain, so
the rule is limited to where the surplus is stuffed.

**Measured (local; faac-benchmark 9f5f102, Zimtohrli via the benchmark's scorer; realized bitrate):**
- 48 kHz stereo LC CBR 128-320, 77 clips: BD-rate vs master -0.73 % [-0.80, -0.64] (FAAD2), -0.75 % (FFmpeg),
  -0.76 % (FDK decoder), PCHIP; linear -0.75..-0.78 %; 72-74/77 clips better. Worst clip x rung -0.018 (10xh_, 128k).
- vs Apple AAC (packaged CBR streams): -1.6..-1.8 %, CI excludes 0 (master CBR: unresolved).
- Benchmark runner, FAAD3, all three profiles, on the previously failing scenarios and neighbours (16k mono 20/24,
  VoIP 24, 24k mono 28/32, 32k stereo 16, 48k stereo 16/24/40/80): bit-identical to master. 48k stereo 128/192:
  LC +0.0030 / +0.0010 mean MOS, HE-v1 +0.0008 / +0.0001, HE-v2 identical; no clip below -0.02.
- Realized rate +0.00..+0.12 %. callgrind 10 s CBR: velvet +4.4 % (192k) / +1.1 % (64k), Bachpsichord +0.3 / +0.1 %.
- Footprint (release, non-LTO): .text +256 B.

**Limits:** 44.1/32 kHz stereo >= 64 kbps/ch CBR is covered by the CI run, not locally; the 64 kbps/ch scope and
the half-capacity point were set from the gate failures, not tuned.
