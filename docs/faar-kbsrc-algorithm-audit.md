# KBSRC algorithm review for FAAR

The current matched sweep and candidate assessment are summarized in the
[recovery diagnosis](faar-recovery-diagnosis.md). Older timing below is
exploratory and is superseded by the matched sweep in the current dataset.

## Source and license

Reviewed HongYue1/KBSRC at commit
[`9bb02260d795350b12ac6632fed40343bffd7b86`](https://github.com/HongYue1/KBSRC/tree/9bb02260d795350b12ac6632fed40343bffd7b86).
The pinned checkout's `LICENSE` is the MIT License, copyright HongYue1 (2026).
The source was cloned and inspected locally; no KBSRC code or test code has
been copied into FAAR. The algorithms below are implementation ideas and are
being written independently in FAAR's C code.

## Useful design ideas

KBSRC computes a rational resampling transform in blocks. For reduced ratio
`P/Q`, its block geometry maps `Nb = nb*Q` input frequency bins to
`Mb = nb*P` output bins. This keeps the transform aligned with the exact
rational rate relationship and allows overlap-save processing.

Its custom FFT uses mixed-radix Stockham passes for factors 2, 3, 4, 5, and 7,
then a four-step decomposition to expose independent transforms to SIMD. It
uses Bluestein for other lengths. The filter is a Kaiser-windowed sinc whose
transition band ends below the lower Nyquist frequency. Coefficient generation
uses double-double arithmetic and integer reduction of the sinc argument.
KBSRC also applies Burg LPC edge extension to whole-file input; this is not a
drop-in fit for FAAR's streaming endpoint contract, so it is not part of the
initial adaptation.

FAAR now has an independently implemented double FFT supporting sizes whose
prime factors are 2, 3, 5, and 7, with a radix-2 fast path. Its standalone
checks compare transforms directly against the DFT and verify inverse round
trips. FAAR also has an independently implemented overlap-save FIR primitive.
The FFT and overlap-save primitives are used by the selected staged-short
backend. KBSRC itself remains an algorithmic reference and is not linked into
FAAR.

## Exploratory timing

The AMD64 measurement used input SHA-256
`7d206aaa491e47bda80f5603dadb50ac02e459aa7052cd8d1e11dca35fe97f0c`,
2,938,880 stereo frames at 96 kHz, converted to 44.1 kHz. KBSRC ran its native
release build in `fast`, one-thread mode with defaults (`len=0.2`, `beta=40`,
f64 output); ARDFTSRC used quality 16384 and bandwidth 0.99. Seven alternating
runs after one warmup produced median reported resampling times of 0.057 s for
KBSRC and 0.0655 s for ARDFTSRC.

This is exploratory data, not a promotion result. KBSRC prints its internal
resampling timer at millisecond precision and includes LPC edge extension;
ARDFTSRC reports processing time at finer precision. The filters and quality
were not matched, output quality parity was not assessed, and KBSRC did not
report output frame counts in this run. These numbers therefore cannot
establish that KBSRC is faster at comparable quality. Raw runs and provenance
are in [`measurements/kbsrc-ardft-cpu-2026-10-18.json`](measurements/kbsrc-ardft-cpu-2026-10-18.json).

Saved outputs from the same run set were also compared with
[`tools/compare_resampler_wavs.py`](../tools/compare_resampler_wavs.py). After
aligning KBSRC and ARDFTSRC by 8,232 output frames and skipping 10,000 frames
at the edge, their first-channel correlation over the next 100,000 frames was
0.999997. Over the remaining interior, KBSRC's residual relative to ARDFTSRC
was −51.40 dB RMS, with a maximum absolute difference of 0.02133. The files use
float64 and float32 WAV output, respectively. This confirms that KBSRC fast
defaults are not an interchangeable ARDFTSRC-quality reference in this probe;
settings are not matched, so the result is diagnostic rather than a
comparable-quality verdict.

Reproduce the metric with:

```sh
python3 tools/compare_resampler_wavs.py \
  /path/to/ardft-kbsrc-0.wav /path/to/kbsrc-fast-0.wav \
  --lag-frames 8232 --skip-frames 10000 --alignment-frames 100000
```

## Next implementation step

Build a ratio-aware stage planner and coefficient designer around the existing
FAAR streaming state machine. First preserve its output count, chunk/capacity
independence, reset semantics, boundary modes, dither, and equal-rate behavior.
Then compare frequency response and CPU time against ARDFTSRC and SoX's CPU
path using matched probes. The required quality and throughput promotion gates
remain those in the [SoX rate audit](faar-sox-rate-audit.md).
