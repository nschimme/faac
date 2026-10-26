#!/usr/bin/env python3
"""Strict PCM regression gate for buffering/implementation optimizations.

Requires NumPy. Uses the public ABI of two independently built shared libraries.
Signals cover the categories described at https://src.hydrogenaudio.org/technical;
this is not a reproduction of that site's scoring or plotting implementation.
"""

import argparse
import ctypes as ct
import hashlib
import json
import math
from pathlib import Path

import numpy as np


class Config(ct.Structure):
    _fields_ = [
        ("struct_size", ct.c_uint32), ("in_rate", ct.c_uint32),
        ("out_rate", ct.c_uint32), ("channels", ct.c_uint32),
        ("quality", ct.c_int32), ("boundary", ct.c_int32),
        ("flags", ct.c_uint32),
        ("decimate_factor", ct.c_uint32), ("max_input_frames", ct.c_uint32),
    ]


DITHER_16 = 0x4


FLOAT_PTR = ct.POINTER(ct.c_float)


class Library:
    def __init__(self, path, prepare=False, drain_each_input=False):
        self.lib = ct.CDLL(str(Path(path).resolve()))
        self.prepare = prepare
        self.drain_each_input = drain_each_input
        signatures = {
            "faar_config_init": [ct.POINTER(Config), ct.c_uint32],
            "faar_resampler_open": [ct.POINTER(Config), ct.POINTER(ct.c_void_p)],
            "faar_resampler_process": [ct.c_void_p, FLOAT_PTR, ct.c_size_t,
                                       FLOAT_PTR, ct.c_size_t, ct.POINTER(ct.c_size_t)],
            "faar_resampler_finalize": [ct.c_void_p, FLOAT_PTR, ct.c_size_t,
                                     ct.POINTER(ct.c_size_t)],
            "faar_resampler_close": [ct.POINTER(ct.c_void_p)],
        }
        for name, args in signatures.items():
            fn = getattr(self.lib, name)
            fn.argtypes, fn.restype = args, ct.c_int32

    @staticmethod
    def check(status):
        if status:
            raise RuntimeError(f"FAAR status {status}")

    def convert(self, data, in_rate, out_rate, quality, boundary=1,
                schedule="regular", dither=0):
        data = np.ascontiguousarray(data, dtype=np.float32)
        if data.ndim == 1:
            data = data[:, None]
        frames, channels = data.shape
        config = Config()
        self.check(self.lib.faar_config_init(ct.byref(config), ct.sizeof(config)))
        config.in_rate, config.out_rate = in_rate, out_rate
        config.channels, config.quality = channels, quality
        config.boundary = boundary
        config.flags = (dither | DITHER_16) if dither else 0
        if self.prepare:
            config.max_input_frames = 8192
        handle = ct.c_void_p()
        self.check(self.lib.faar_resampler_open(ct.byref(config), ct.byref(handle)))
        expected = (frames * out_rate + in_rate - 1) // in_rate
        result = np.empty((expected, channels), dtype=np.float32)
        buffer = np.empty((16384, channels), dtype=np.float32)
        ptr = buffer.ctypes.data_as(FLOAT_PTR)
        made = ct.c_size_t()
        written = 0

        def save():
            nonlocal written
            if written + made.value > expected:
                raise AssertionError("excess output")
            result[written:written + made.value] = buffer[:made.value]
            written += made.value

        try:
            position, step = 0, 0
            while position < frames:
                # The queued schedule appends again before draining, exercising
                # compaction, growth with a nonzero offset, and zero capacity.
                block = 4096 if schedule == "regular" else (1, 17, 4096, 257, 8192)[step % 5]
                capacity = 16384 if schedule == "regular" else (0, 1, 17, 257, 4096)[step % 5]
                count = min(block, frames - position)
                src = data[position:].ctypes.data_as(FLOAT_PTR)
                self.check(self.lib.faar_resampler_process(
                    handle, src, count, ptr, capacity, ct.byref(made)))
                save()
                position += count
                step += 1
                if schedule == "regular" or self.drain_each_input:
                    while made.value or self.drain_each_input:
                        self.check(self.lib.faar_resampler_process(
                            handle, None, 0, ptr, 16384, ct.byref(made)))
                        save()
                        if not made.value:
                            break
            # Bound the loop so broken flush behavior fails instead of hanging.
            for _ in range(expected + 2):
                self.check(self.lib.faar_resampler_finalize(
                    handle, ptr, 16384, ct.byref(made)))
                save()
                if not made.value:
                    break
            else:
                raise AssertionError("flush did not terminate")
            if written != expected or not np.isfinite(result).all():
                raise AssertionError("invalid output count or non-finite PCM")
        finally:
            self.check(self.lib.faar_resampler_close(ct.byref(handle)))
        return result


def signals():
    rate = 96000
    t = np.arange(rate * 20, dtype=np.float64) / rate
    yield "sweep_1_44000", np.sin(2 * np.pi * (t + .5 * (43999 / 20) * t * t))
    t = np.arange(rate * 30, dtype=np.float64) / rate
    tone = 10 ** (-4 / 20) * np.sin(2 * np.pi * 23000 * t)
    yield "alias_23000", tone
    # Explicit RMS convention and fixed seed: suitable for repeatable comparison.
    noise = np.random.default_rng(20261024).standard_normal(t.size) * 10 ** (-150 / 20)
    yield "alias_23000_noise", tone + noise
    yield "two_tone", (10 ** (-6 / 20) * np.sin(2 * np.pi * 64.599609375 * t)
                       + 10 ** (-18.0412 / 20) * np.sin(2 * np.pi * 6998.291015625 * t))
    # Coprime spacing covers every n mod 320 and n mod 147; impulses are
    # separated enough to inspect the long High filter's ringing independently.
    stride = 8191
    assert math.gcd(stride, 320) == math.gcd(stride, 147) == 1
    impulses = np.zeros(stride * 322)
    impulses[stride:stride * 321:stride] = 1
    yield "phase_impulses", impulses
    t = np.arange(rate // 5) / rate
    yield "boundary_tones", (10 ** (-5 / 20) * np.sin(2 * np.pi * 239 * t)
                             + 10 ** (-7.5 / 20) * np.sin(2 * np.pi * 1000 * t))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", help="baseline shared library")
    parser.add_argument("candidate", help="optimized shared library")
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--qualities", nargs="+", type=int, default=[0, 1, 2], choices=[0, 1, 2])
    parser.add_argument("--prepare", action="store_true",
                        help="prepare candidate for 8192 frames; drain both streams between input chunks")
    args = parser.parse_args()
    reference = Library(args.reference, drain_each_input=args.prepare)
    candidate = Library(args.candidate, prepare=args.prepare, drain_each_input=args.prepare)
    records = []

    def compare(name, data, ir, out, q, boundary=1, schedule="regular", dither=0):
        old = reference.convert(data, ir, out, q, boundary, schedule, dither)
        new = candidate.convert(data, ir, out, q, boundary, schedule, dither)
        # Compare IEEE representations, including signed zeros, not a tolerance.
        equal = np.array_equal(old.view(np.uint32), new.view(np.uint32))
        record = dict(signal=name, in_rate=ir, out_rate=out, quality=q,
                      channels=new.shape[1], boundary=boundary, schedule=schedule,
                      dither=dither, frames=len(new), bit_identical=equal,
                      max_error=float(np.max(np.abs(new.astype(np.float64) - old))),
                      peak=float(np.max(np.abs(new))),
                      sha256=hashlib.sha256(new.tobytes()).hexdigest())
        records.append(record)
        if not equal:
            raise AssertionError(f"quality regression: {record}")
        return new

    try:
        for name, signal in signals():
            pcm = (signal * 32768).astype(np.float32)
            for q in args.qualities:
                for schedule in ("regular", "queued"):
                    y = compare(name, pcm, 96000, 44100, q, schedule=schedule)
                    if name == "alias_23000":
                        # Exclude startup/end extension to isolate steady-state rejection.
                        rms = np.sqrt(np.mean((y[44100:-44100, 0].astype(np.float64) / 32768) ** 2))
                        records[-1]["alias_rejection_db"] = float(20 * np.log10(
                            (10 ** (-4 / 20) / np.sqrt(2)) / max(rms, 1e-300)))
                        target = (100.0, 130.0, 150.0)[q]
                        records[-1]["alias_rejection_min_db"] = target
                        if records[-1]["alias_rejection_db"] < target:
                            raise AssertionError(f"23 kHz rejection below preset target: {records[-1]}")
                if name == "boundary_tones":
                    # Independently opened track halves are distinct from a
                    # continuous stream; preserve their edge artifacts too.
                    for boundary in (0, 1):
                        for part in (pcm[:3287], pcm[3287:5696]):
                            compare("independent_track_edge", part, 96000, 44100, q, boundary)
        # Cover other planner routes, channel layouts, boundary modes, and RNG.
        t = np.arange(12000, dtype=np.float64)
        for ir, out in ((48000, 44100), (44100, 48000), (192000, 44100),
                        (96000, 48000), (48000, 48000), (47999, 44100)):
            for q in args.qualities:
                for channels in (1, 2, 3, 8):
                    pcm = np.stack([16000 * np.sin(t * (.137 + c * .019))
                                    for c in range(channels)], axis=1)
                    for boundary in (0, 1):
                        for dither in (0, 1, 2):
                            compare("route_matrix", pcm, ir, out, q, boundary, "queued", dither)
    finally:
        args.report.write_text(json.dumps(dict(
            reference=str(Path(args.reference).resolve()),
            candidate=str(Path(args.candidate).resolve()),
            candidate_prepared=args.prepare, cases=records), indent=2) + "\n")
    print(f"PASS: {len(records)} cases, all output PCM bit-identical")


if __name__ == "__main__":
    main()
