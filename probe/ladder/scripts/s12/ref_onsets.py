#!/usr/bin/env python3
"""Reference onsets (eligible/scored events) using the benchmark's own transient.py.

onsets(wav_path) -> list of (channel, onset_sample_48k).  Events are exactly those
transient._scored_pre_echo_events yields when the decoded signal equals the reference
(lag 0), i.e. the reference-side eligibility (candidates -> refined -> eligible) used
by pre_echo_metrics.  Per-channel events; the same instant may appear for ch0 and ch1.
CLI: ref_onsets.py out.json  (all clips in $AUDIO)
"""
import os, sys, json, glob
FB = os.environ.get("FB", "/home/user/work/fb")
sys.path.insert(0, FB)
import numpy as np, scipy.signal, soundfile as sf
import transient as T


def onsets(wav):
    ref, sr = sf.read(wav, dtype="float32", always_2d=True)
    ref = np.asarray(ref, dtype=np.float64)
    ref48 = T._resample_audio(ref, int(sr))
    sos = scipy.signal.butter(4, 2000, btype='highpass', fs=T.PRE_ECHO_RATE, output='sos')
    hp = scipy.signal.sosfilt(sos, ref48, axis=0)
    cov = {'candidates': 0, 'refined': 0, 'eligible': 0, 'scored': 0}
    ev = [(int(e[0]), int(e[1])) for e in T._scored_pre_echo_events(ref48, hp, hp, 0, cov)]
    return ev, cov, int(sr), len(ref48)


if __name__ == "__main__":
    audio = os.environ.get("AUDIO", FB + "/data/external/audio")
    out = {}
    for w in sorted(glob.glob(audio + "/*.wav")):
        ev, cov, sr, n = onsets(w)
        out[os.path.basename(w)] = {"events": ev, "coverage": cov, "sr": sr, "n": n}
    json.dump(out, open(sys.argv[1], "w"))
    print(sum(len(v["events"]) for v in out.values()), "events",
          {k: sum(v["coverage"][k] for v in out.values()) for k in ("candidates", "refined", "eligible", "scored")})
