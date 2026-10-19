#!/usr/bin/env python3
"""Compare a libfaar build with ARDFTSRC on deterministic mono tone probes."""
import argparse
import ctypes as ct
import hashlib
import json
import math
import struct
import subprocess
import tempfile
from math import gcd
from pathlib import Path

import numpy as np


def digest(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def write_float_wav(path, samples, rate):
    raw = np.asarray(samples, dtype="<f4").tobytes()
    fmt = struct.pack("<HHIIHH", 3, 1, rate, rate * 4, 4, 32)
    chunks = (b"fmt " + struct.pack("<I", len(fmt)) + fmt
              + b"data" + struct.pack("<I", len(raw)) + raw)
    Path(path).write_bytes(b"RIFF" + struct.pack("<I", len(chunks) + 4)
                           + b"WAVE" + chunks)


def read_float_wav(path):
    raw = Path(path).read_bytes()
    if raw[:4] != b"RIFF" or raw[8:12] != b"WAVE":
        raise ValueError(f"not a RIFF/WAVE file: {path}")
    offset, fmt = 12, None
    while offset + 8 <= len(raw):
        tag = raw[offset:offset + 4]
        size = struct.unpack_from("<I", raw, offset + 4)[0]
        start = offset + 8
        if start + size > len(raw):
            raise ValueError(f"truncated WAV chunk: {path}")
        if tag == b"fmt ":
            fmt = struct.unpack_from("<HHIIHH", raw, start)
        elif tag == b"data":
            if fmt is None or fmt[0] != 3 or fmt[1] != 1 or fmt[5] != 32:
                raise ValueError("ARDFTSRC output must be mono float32 WAV")
            return np.frombuffer(raw, dtype="<f4", count=size // 4,
                                 offset=start).astype(np.float64), fmt[2]
        offset = start + size + (size & 1)
    raise ValueError(f"missing float data chunk: {path}")


class Faar:
    def __init__(self, path):
        self.lib = ct.CDLL(str(path))
        self.ptr = ct.POINTER(ct.c_float)
        self.lib.faar_create.argtypes = [ct.c_uint32, ct.c_uint32,
                                         ct.c_uint16, ct.c_int]
        self.lib.faar_create.restype = ct.c_void_p
        self.lib.faar_process.argtypes = [ct.c_void_p, self.ptr, ct.c_size_t,
                                          self.ptr, ct.c_size_t]
        self.lib.faar_process.restype = ct.c_size_t
        self.lib.faar_flush.argtypes = [ct.c_void_p, self.ptr, ct.c_size_t]
        self.lib.faar_flush.restype = ct.c_size_t
        self.lib.faar_destroy.argtypes = [ct.c_void_p]

    def convert(self, samples, in_rate, out_rate, quality, block):
        handle = self.lib.faar_create(in_rate, out_rate, 1, quality)
        if not handle:
            raise RuntimeError("faar_create failed")
        source = (np.asarray(samples, dtype=np.float32)
                  * np.float32(32768.0)).astype(np.float32)
        output = np.empty(65536, dtype=np.float32)
        result = []
        try:
            for start in range(0, len(source), block):
                chunk = source[start:start + block]
                n = self.lib.faar_process(handle, chunk.ctypes.data_as(self.ptr),
                    len(chunk), output.ctypes.data_as(self.ptr), len(output))
                if n:
                    result.append(output[:n].copy())
                while True:
                    n = self.lib.faar_process(handle, None, 0,
                        output.ctypes.data_as(self.ptr), len(output))
                    if not n:
                        break
                    result.append(output[:n].copy())
            while True:
                n = self.lib.faar_flush(handle, output.ctypes.data_as(self.ptr),
                                        len(output))
                if not n:
                    break
                result.append(output[:n].copy())
        finally:
            self.lib.faar_destroy(handle)
        return np.concatenate(result).astype(np.float64) / 32768.0


def measure(samples, source, in_rate, out_rate, frequency, peak):
    start, end = out_rate, 2 * out_rate
    y = samples[start:end]
    t = np.arange(start, end, dtype=np.float64) / out_rate
    output_hz = frequency % out_rate
    if output_hz > out_rate / 2:
        output_hz = out_rate - output_hz
    matrix = np.column_stack((np.sin(2 * np.pi * output_hz * t),
                              np.cos(2 * np.pi * output_hz * t),
                              np.ones_like(t)))
    coeff, _, _, _ = np.linalg.lstsq(matrix, y, rcond=None)
    residual = y - matrix @ coeff
    input_rms = float(np.sqrt(np.mean(source[in_rate:2 * in_rate].astype(np.float64) ** 2)))
    output_rms = float(np.sqrt(np.mean(y * y)))
    wanted = float(np.hypot(coeff[0], coeff[1]))
    return {"wanted_gain_db": 20 * math.log10(wanted / peak),
            "wanted_phase_radians": math.atan2(float(coeff[1]), float(coeff[0])),
            "residual_relative_input_db": 20 * math.log10(
                float(np.sqrt(np.mean(residual * residual))) / input_rms),
            "alias_attenuation_db": 20 * math.log10(input_rms / output_rms)
                if frequency > out_rate / 2 else None}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", required=True)
    parser.add_argument("--reference", required=True, help="ARDFTSRC executable")
    parser.add_argument("--in-rate", type=int, default=96000)
    parser.add_argument("--out-rate", type=int, default=44100)
    parser.add_argument("--quality", type=int, choices=(0, 1, 2), default=2)
    parser.add_argument("--reference-quality", type=int, default=16384)
    parser.add_argument("--bandwidth", type=float, default=0.99)
    parser.add_argument("--seconds", type=int, default=4)
    parser.add_argument("--peak", type=float, default=0.5)
    parser.add_argument("--probes", type=int, nargs="+",
                        default=(1000, 20000, 22060, 23000))
    parser.add_argument("--block", type=int, default=4096)
    args = parser.parse_args()
    if args.in_rate < 1 or args.out_rate < 1 or args.seconds < 3 or args.block < 1:
        parser.error("rates/block must be positive and seconds must be at least three")
    if not (0 < args.peak <= 1) or not (0 < args.bandwidth < 1):
        parser.error("peak and bandwidth are outside valid ranges")
    library, reference = Path(args.library).resolve(), Path(args.reference).resolve()
    converter = Faar(library)
    divisor = gcd(args.in_rate, args.out_rate)
    output_unit = args.out_rate // divisor
    factor = 2 * ((args.reference_quality + 2 * output_unit - 1)
                  // (2 * output_unit))
    tail = (args.in_rate // divisor) * factor
    delay = output_unit * factor // 2
    report = {"input_rate": args.in_rate, "output_rate": args.out_rate,
              "seconds": args.seconds, "peak": args.peak,
              "quality": args.quality, "block_frames": args.block,
              "reference_quality": args.reference_quality,
              "reference_bandwidth": args.bandwidth,
              "library": str(library), "library_sha256": digest(library),
              "reference": str(reference), "reference_sha256": digest(reference),
              "reference_delay_frames": delay, "reference_tail_frames": tail,
              "measurements": []}
    with tempfile.TemporaryDirectory(prefix="faar-ardft-compare-") as td:
        for frequency in args.probes:
            source = (args.peak * np.sin(2 * np.pi * frequency
                      * np.arange(args.seconds * args.in_rate) / args.in_rate)).astype(np.float32)
            candidate = converter.convert(source, args.in_rate, args.out_rate,
                                          args.quality, args.block)
            input_wav, output_wav = Path(td) / "input.wav", Path(td) / "reference.wav"
            padded = np.concatenate((source, np.zeros(tail, dtype=np.float32)))
            write_float_wav(input_wav, padded, args.in_rate)
            command = [str(reference), str(input_wav), str(output_wav),
                       str(args.out_rate), "32", str(args.reference_quality),
                       str(args.bandwidth)]
            subprocess.run(command, check=True, stdout=subprocess.DEVNULL,
                           stderr=subprocess.PIPE, text=True)
            raw_reference, rate = read_float_wav(output_wav)
            if rate != args.out_rate:
                raise ValueError("ARDFTSRC output rate mismatch")
            expected = args.seconds * args.out_rate
            aligned = raw_reference[delay:delay + expected]
            if len(candidate) != expected or len(aligned) != expected:
                raise ValueError("candidate/reference output frame count mismatch")
            row = {"frequency_hz": frequency,
                   "candidate": measure(candidate, source, args.in_rate,
                                        args.out_rate, frequency, args.peak),
                   "ardftsrc": measure(aligned, source, args.in_rate,
                                       args.out_rate, frequency, args.peak),
                   "reference_command": command}
            for metric in ("wanted_gain_db", "residual_relative_input_db",
                           "alias_attenuation_db"):
                a, b = row["candidate"][metric], row["ardftsrc"][metric]
                row[metric + "_absolute_difference_db"] = (
                    abs(a - b) if a is not None and b is not None else None)
            report["measurements"].append(row)
    print(json.dumps(report, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
