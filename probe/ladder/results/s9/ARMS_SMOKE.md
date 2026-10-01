# HE 48k arm smoke

All requested injection arms, two 40/56 slope anchors, and both raw references completed for `12-German-male-speech.441.16b48k.wav` with `NUMBA_DISABLE_JIT=1`. Each clip-arm took 2.1–2.8 seconds, including encode or reference lookup, FAAD decision dump, and `score_clip.py`. A second `W_fdk --limit 1` invocation skipped its saved row. The 40/56 per-clip log2-byte slope summary runs.

The pad-0 versus pad-2015 MOS control is material: German speech 4.1566 → 4.1980 (+0.0414), Good evening 4.0610 → 4.0300 (−0.0310), Classic 4.3274 → 4.3249 (−0.0025). Only Classic meets the requested 0.01 tolerance. The encoded window shares change with the pad, so the arm summaries compare each injected arm with its own padded F baseline. This smoke does not establish a corpus quality gain.

On German speech, `F_fdk` and `F_apple` are 86.6% and 88.6% short. The W outputs are 7.0% and 8.1% short. `MS_apple` changes long regular-band M/S share from 100.0% in `F_apple` to 7.2%; the Apple HE reference is 8.4%. `MS_fdk` is byte/MOS-identical to `F_fdk` on this clip, consistent with the 100% M/S share already present in the FDK reference's long regular bands. WS is a rate-loop scalefactor-injection diagnostic; its one-clip losses must not be interpreted as an SF oracle result.

The one-clip bits-adjusted mean versus each arm's own F is W_fdk −0.4162, MS_fdk +0.0000, WMS_fdk −0.4162, WS_fdk −0.7800; W_apple −0.3700, MS_apple −0.0944, WMS_apple −1.1113, WS_apple −0.6868. Raw reference MOS − F is +0.1632 for FDK and −0.1502 for Apple on this clip. These are smoke results only.
