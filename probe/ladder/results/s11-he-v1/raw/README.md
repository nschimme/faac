# HE-AAC v1 feature investigation

Encoder base: `upstream/master`, `99ce2b0980180194f9ccd1993a786208d277cf3e`.
Decoder inventory: the original checkout's `libfaad`, commit `333fe311`.
The base encoder does not contain the in-tree decoder. The original checkout
and its untracked `AGENTS.md` are left untouched.

These changes are experimental probes, not production features. Environment
variables change one factor per run; all unset probes must reproduce baseline
AAC packets and decoded PCM. MP4 metadata includes the build version, so whole
container equality is not an appropriate control for a dirty probe build.

## Inventory

| Tool | Base encoder behavior | Experiment |
|---|---|---|
| Envelope time/frequency deltas | Already selected by coded bit cost | No duplicate implementation |
| Stereo coupling | Already selected by coded bit cost | No duplicate implementation |
| Temporal grids | FIXFIX/VARFIX selected from transients | Existing work; not retuned here |
| Inverse filtering | Constant HIGH (3) | OFF/LOW/MID, one at a time |
| Noise floor | One noise band, constant level 12 | Levels 6/9/15/18 |
| Smoothing | Header default mode 1 (smoothing disabled) | Mode 0 |
| Frequency interpolation | Header default 1 | Mode 0 |
| Limiter gains/bands | Header defaults 2/2 | Alternatives 0/1/3 separately |
| Additional harmonics | Never signaled | Energy-concentration thresholds 60/75/90 percent |

Harmonic screening requires a high-resolution band at least three QMF bins
wide. Its midpoint must carry the threshold fraction of band energy in every
envelope. This uses existing analysis only, but does not compare original and
replicated-source tonality. Failure rejects this heuristic, not the entire
missing-harmonic tool. Similarly, constant noise/inverse-filter sweeps do not
validate or reject a full adaptive detector.

## Measurement

Run from `/Users/nschimme/gitprojects/faac-benchmark` using its `.venv/bin/python`:

```sh
NUMBA_DISABLE_JIT=1 .venv/bin/python /private/tmp/faac-he-v1-probes/probe/run_features.py
```

The benchmark's fixed music gate has four 48 kHz stereo clips. Rates are
32/48/64/96 kbps total, forced HE-AAC v1 and independently checked by ffprobe.
Strict FDK decoding precedes scoring with `phase2_mos.score_wav_pair` in audio
mode (Zimtohrli). Actual payload bitrate is AAC packet bytes divided by source
duration. `scripts/bd_rate.py` computes PCHIP BD-rate from those actual rates.
Negative BD-rate means fewer bits for equal metric quality.

Gate results only select directions to investigate. A production proposal
requires a wider corpus, fresh comparison with the same base, independent
decoder checks, and appropriate speed/footprint validation. No gain is assumed
from the number of syntax tools a decoder implements. The release encoder
base defines no Meson tests; builds alone are not a quality result.

The public encoder specification describes noise-floor and inverse-filter
estimation using original and patched signal tonality:
[3GPP TS 26.404](https://www.etsi.org/deliver/etsi_TS/126400_126499/126404/19.00.00_60/ts_126404v190000p.pdf).
No external implementation source was copied.
