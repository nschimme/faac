#!/usr/bin/env python3
"""TNS quality checks: encoder/decoder agreement, and pre-echo reduction.

Two things the benchmark corpus cannot tell us:

  roundtrip  At a bitrate high enough that the quantizer is transparent, any
             residual error is the TNS analysis/synthesis pair failing to
             cancel. Catches encoder/decoder disagreement over band range,
             filter direction or coefficients -- a class of bug that is
             otherwise invisible, because the output still sounds plausible.

  preecho    TNS redistributes quantization noise in time rather than reducing
             it, so global SNR is blind to it by construction, and ViSQOL MOS
             is too noisy to resolve a few tenths of a dB. This measures coding
             noise in the quiet lead-in before each attack -- the artifact TNS
             exists to suppress. Short blocks are disabled so that TNS is the
             only tool in play; for calibration, re-enabling them moves this
             metric by ~12 dB.

Usage:  tns_quality.py <faac-binary> [clip.wav ...]

Test signals are synthesised when no clips are given, so this runs anywhere.
Requires ffmpeg (decode) and numpy.
"""
import os
import subprocess
import sys
import tempfile
import wave

import numpy as np

SR = 44100
ROUNDTRIP_MAX_LOSS_DB = 1.0


def synth(path, kind):
    """Tonal attacks (what TNS is for) or noise bursts (what it should skip)."""
    n = SR * 6
    x = np.zeros(n)
    rs = np.random.RandomState(7)
    if kind == "tonal":
        # glockenspiel-like: ~1 ms attack, strongly harmonic decay
        for on in np.arange(0.3, n / SR - 0.5, 0.45):
            i, L = int(on * SR), int(0.40 * SR)
            f0 = rs.choice([523.25, 659.25, 783.99, 1046.5])
            t = np.arange(L) / SR
            v = np.zeros(L)
            for h, amp in ((1, 1.0), (2, .6), (3, .35), (4, .2), (5, .12), (7, .08)):
                v += amp * np.sin(2 * np.pi * f0 * h * t + rs.rand() * 6.28) * \
                     np.exp(-t / (0.25 / h ** 0.5))
            v *= np.minimum(1.0, np.arange(L) / (0.0008 * SR))
            x[i:i + L] += v
    else:
        for on in np.arange(0.25, n / SR - 0.2, 0.32):
            i, L = int(on * SR), int(0.09 * SR)
            x[i:i + L] += np.exp(-np.arange(L) / (0.012 * SR)) * rs.randn(L) * 0.7
    x /= np.abs(x).max() * 1.05
    d = (np.stack([x, x], 1) * 32767).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(d.tobytes())


def decode(path):
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-f", "f32le",
                          "-ac", "2", "-ar", str(SR), "-"],
                         capture_output=True, check=True).stdout
    return np.frombuffer(raw, dtype=np.float32).reshape(-1, 2)


def encode(faac, src, out, tns, extra=()):
    subprocess.run([faac, "--tns" if tns else "--no-tns", *extra,
                    "-o", out, "-v0", src], check=True, capture_output=True)


def find_attacks(mono):
    hop = 128
    env = np.array([np.sum(mono[i:i + hop] ** 2) for i in range(0, len(mono) - hop, hop)])
    rise = np.diff(10 * np.log10(env + 1e-12))
    ons = []
    for i in range(1, len(rise) - 1):
        if rise[i] > 12 and rise[i] >= rise[i - 1] and rise[i] > rise[i + 1]:
            o = (i + 1) * hop
            if not ons or o - ons[-1] > 0.05 * SR:
                ons.append(o)
    return ons


def preecho_db(src, test, ons, shift=1024):
    """Absolute coding-noise energy in the 20 ms before each attack."""
    n = min(len(src), len(test) - shift)
    err = (src[:n] - test[shift:shift + n]).mean(1)
    pre, post = int(0.020 * SR), int(0.005 * SR)
    tot = cnt = 0
    for o in ons:
        s, e = o - pre, o - post
        if s < 0 or e > n:
            continue
        tot += np.sum(err[s:e] ** 2)
        cnt += 1
    return 10 * np.log10(tot / max(cnt, 1) + 1e-20)


def snr_db(src, test, shift=1024):
    n = min(len(src), len(test) - shift)
    a, b = src[:n], test[shift:shift + n]
    return 10 * np.log10(np.sum(a ** 2) / np.sum((a - b) ** 2))


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    faac, clips = sys.argv[1], sys.argv[2:]
    failures = []

    with tempfile.TemporaryDirectory() as td:
        if not clips:
            for kind in ("tonal", "noise"):
                p = os.path.join(td, kind + ".wav")
                synth(p, kind)
                clips.append(p)

        for clip in clips:
            name = os.path.basename(clip)
            src = decode(clip)
            ons = find_attacks(src.mean(1))

            snrs = {}
            for tns in (False, True):
                out = os.path.join(td, "rt.aac")
                encode(faac, clip, out, tns, ("-b", "320"))
                snrs[tns] = snr_db(src, decode(out))
            loss = snrs[False] - snrs[True]
            ok = loss < ROUNDTRIP_MAX_LOSS_DB
            print(f"  {name:14s} roundtrip  off {snrs[False]:6.2f} dB  on {snrs[True]:6.2f} dB"
                  f"   {'ok' if ok else 'FAIL: encoder/decoder disagree'}")
            if not ok:
                failures.append(f"{name}: TNS costs {loss:.2f} dB at a transparent bitrate")

            if not ons:
                continue
            pe = {}
            for tns in (False, True):
                out = os.path.join(td, "pe.aac")
                encode(faac, clip, out, tns, ("-b", "96", "--shortctl", "1"))
                pe[tns] = preecho_db(src, decode(out), ons)
            print(f"  {name:14s} pre-echo   off {pe[False]:6.2f} dB  on {pe[True]:6.2f} dB"
                  f"   delta {pe[True] - pe[False]:+.2f} dB  ({len(ons)} attacks)")

    if failures:
        print("\nFAILURES:")
        for f in failures:
            print("  " + f)
        return 1
    print("\nTNS quality checks passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
