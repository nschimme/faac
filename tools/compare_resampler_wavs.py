#!/usr/bin/env python3
"""Compare two IEEE-float WAV resampler outputs after an explicit frame shift."""
import argparse
import array
import hashlib
import json
import math
import sys
from pathlib import Path


def read_float_wav(path):
    raw = Path(path).read_bytes()
    # Parse `fmt ` and `data` ourselves to support float32 and float64 WAVs.
    if raw[:4] != b"RIFF" or raw[8:12] != b"WAVE":
        raise ValueError(f"{path}: expected RIFF/WAVE")
    offset = 12
    fmt = None
    payload = None
    while offset + 8 <= len(raw):
        tag = raw[offset:offset + 4]
        size = int.from_bytes(raw[offset + 4:offset + 8], "little")
        start = offset + 8
        end = start + size
        if end > len(raw):
            raise ValueError(f"{path}: truncated WAV chunk")
        if tag == b"fmt ":
            if size < 16:
                raise ValueError(f"{path}: short fmt chunk")
            code = int.from_bytes(raw[start:start + 2], "little")
            channels = int.from_bytes(raw[start + 2:start + 4], "little")
            rate = int.from_bytes(raw[start + 4:start + 8], "little")
            bits = int.from_bytes(raw[start + 14:start + 16], "little")
            fmt = (code, channels, rate, bits)
        elif tag == b"data":
            payload = (start, size)
            break
        offset = end + (size & 1)
    if fmt is None or payload is None:
        raise ValueError(f"{path}: missing fmt or data chunk")
    code, channels, rate, bits = fmt
    if code != 3 or bits not in (32, 64) or channels < 1:
        raise ValueError(f"{path}: expected IEEE float32 or float64 WAV")
    start, size = payload
    values = array.array("f" if bits == 32 else "d")
    values.frombytes(raw[start:start + size])
    if sys.byteorder != "little":
        values.byteswap()
    if len(values) % channels:
        raise ValueError(f"{path}: data does not contain whole frames")
    if not all(math.isfinite(value) for value in values):
        raise ValueError(f"{path}: non-finite sample")
    return values, {"channels": channels, "rate": rate, "bits": bits,
                    "frames": len(values) // channels,
                    "sha256": hashlib.sha256(raw).hexdigest()}


def compare(reference, candidate, channels, lag, skip, align_frames):
    ref_frames = len(reference) // channels
    cand_frames = len(candidate) // channels
    ref_start = max(lag, 0) + skip
    cand_start = max(-lag, 0) + skip
    count = min(ref_frames - ref_start, cand_frames - cand_start)
    if count <= 0 or align_frames <= 0:
        raise ValueError("alignment leaves no frames to compare")

    corr_n = min(count, align_frames)
    dot = ref_energy = cand_energy = 0.0
    for frame in range(corr_n):
        x = reference[(ref_start + frame) * channels]
        y = candidate[(cand_start + frame) * channels]
        dot += x * y
        ref_energy += x * x
        cand_energy += y * y
    correlation = dot / math.sqrt(ref_energy * cand_energy)

    diff_energy = signal_energy = 0.0
    max_abs = 0.0
    sample_count = count * channels
    ref_index = ref_start * channels
    cand_index = cand_start * channels
    for i in range(sample_count):
        x = reference[ref_index + i]
        y = candidate[cand_index + i]
        error = y - x
        diff_energy += error * error
        signal_energy += x * x
        max_abs = max(max_abs, abs(error))
    residual = math.sqrt(diff_energy / sample_count)
    signal = math.sqrt(signal_energy / sample_count)
    return {"lag_frames_reference_minus_candidate": lag,
            "alignment_frames": corr_n,
            "first_channel_alignment_correlation": correlation,
            "compared_frames": count,
            "skip_frames_after_alignment": skip,
            "relative_residual_rms_db": (20 * math.log10(residual / signal)
                                         if residual and signal else None),
            "max_absolute_difference": max_abs}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", help="reference float WAV")
    parser.add_argument("candidate", help="candidate float WAV")
    parser.add_argument("--lag-frames", type=int, required=True,
                        help="reference index offset relative to candidate")
    parser.add_argument("--skip-frames", type=int, default=0,
                        help="skip this many frames after alignment (edge exclusion)")
    parser.add_argument("--alignment-frames", type=int, default=100000)
    args = parser.parse_args()
    if args.skip_frames < 0 or args.alignment_frames <= 0:
        parser.error("skip-frames must be nonnegative and alignment-frames positive")
    reference, ref_info = read_float_wav(args.reference)
    candidate, cand_info = read_float_wav(args.candidate)
    if (ref_info["channels"], ref_info["rate"]) != (cand_info["channels"], cand_info["rate"]):
        parser.error("WAVs must have the same channel count and sample rate")
    print(json.dumps({"reference": ref_info, "candidate": cand_info,
                      "comparison": compare(reference, candidate,
                          ref_info["channels"], args.lag_frames,
                          args.skip_frames, args.alignment_frames)}, indent=2,
                      sort_keys=True))


if __name__ == "__main__":
    main()
