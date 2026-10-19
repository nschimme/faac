#!/usr/bin/env python3
"""Compare FAAR with SoX CPU and Vulkan rate profiles on deterministic PCM.

SoX is invoked as an external reference; this script does not copy or link SoX
code. It reports full-stream counts, output error and child CPU/wall time. The
comparison is diagnostic, not a perceptual score or quality qualification.
"""
import argparse
import hashlib
import json
import math
import os
import resource
import re
import struct
import statistics
import subprocess
import tempfile
import time
from pathlib import Path


def child_cpu_seconds():
    usage = resource.getrusage(resource.RUSAGE_CHILDREN)
    return usage.ru_utime + usage.ru_stime


def input_files(frames, in_rate, channels, sox_input, faar_input):
    hasher = hashlib.sha256()
    state = 0x12345678
    with open(sox_input, "wb") as sx, open(faar_input, "wb") as fa:
        block = 4096
        for start in range(0, frames, block):
            count = min(block, frames - start)
            normalized = []
            faar_pcm = []
            for i in range(start, start + count):
                for c in range(channels):
                    # The fixed probes cover passband, transition and stopband.
                    tone = (0.23 * math.sin(2 * math.pi * 997 * i / in_rate)
                            + 0.19 * math.sin(2 * math.pi * 10000 * i / in_rate)
                            + 0.17 * math.sin(2 * math.pi * 20000 * i / in_rate)
                            + 0.13 * math.sin(2 * math.pi * 22060 * i / in_rate))
                    state ^= (state << 13) & 0xFFFFFFFF
                    state ^= state >> 17
                    state ^= (state << 5) & 0xFFFFFFFF
                    noise = ((state / 4294967296.0) - 0.5) * 0.06
                    sample = tone + noise + c * 0.03125
                    normalized.append(sample)
                    # faar's raw f32 reader scales normalized PCM to its
                    # internal signed-16-bit full-scale convention.
                    faar_pcm.append(sample)
            sox_bytes = struct.pack("<%sf" % len(normalized), *normalized)
            faar_bytes = struct.pack("<%sf" % len(faar_pcm), *faar_pcm)
            sx.write(sox_bytes)
            fa.write(faar_bytes)
            hasher.update(sox_bytes)
    return hasher.hexdigest()


def run(command):
    before_cpu = child_cpu_seconds()
    start = time.perf_counter()
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True, check=False)
    elapsed = time.perf_counter() - start
    cpu = child_cpu_seconds() - before_cpu
    if result.returncode:
        raise RuntimeError("command failed (%d): %s\n%s" %
                           (result.returncode, " ".join(command), result.stderr))
    timing = {"wall_seconds": elapsed, "child_cpu_seconds": cpu,
              "process_wall_seconds": elapsed, "process_cpu_seconds": cpu,
              "stderr": result.stderr.strip()}
    # Podman only accounts for the client process in RUSAGE_CHILDREN. The
    # optional wrapper reports the actual SoX child process from inside the
    # container, avoiding a misleading wrapper-only CPU measurement.
    match = re.search(r"SOX_TIMING real=([0-9.]+) user=([0-9.]+) sys=([0-9.]+)(?: max_rss_kb=([0-9]+))?",
                      result.stderr)
    if match:
        timing["container_process_cpu_seconds"] = float(match.group(2)) + float(match.group(3))
        timing["container_process_wall_seconds"] = float(match.group(1))
        timing["process_cpu_seconds"] = timing["container_process_cpu_seconds"]
        timing["process_wall_seconds"] = timing["container_process_wall_seconds"]
        if match.group(4):
            timing["container_max_rss_kb"] = int(match.group(4))
    # Cgroup v2 reports usage_usec with microsecond resolution. This delta is
    # measured inside the container around SoX and avoids GNU time's 10 ms
    # rounding; it includes only the tiny timing-shell overhead as well.
    cgroup_match = re.search(
        r"SOX_CGROUP_CPU usage_usec=(\d+) user_usec=(\d+) system_usec=(\d+)",
        result.stderr)
    if cgroup_match:
        timing["container_cgroup_cpu_seconds"] = int(cgroup_match.group(1)) / 1_000_000.0
        timing["container_cgroup_user_seconds"] = int(cgroup_match.group(2)) / 1_000_000.0
        timing["container_cgroup_system_seconds"] = int(cgroup_match.group(3)) / 1_000_000.0
        timing["process_cpu_seconds"] = timing["container_cgroup_cpu_seconds"]
    return timing


def read_f32(path):
    data = Path(path).read_bytes()
    if len(data) % 4:
        raise RuntimeError("raw float output has a partial sample: %s" % path)
    return struct.unpack("<%sf" % (len(data) // 4), data)


def timing_summary(runs):
    keys = ("process_cpu_seconds", "process_wall_seconds", "wall_seconds",
            "child_cpu_seconds")
    return {key: statistics.median(run[key] for run in runs) for key in keys}


def expected_frames(frames, in_rate, out_rate):
    return (frames * out_rate + in_rate - 1) // in_rate


def compare(reference, actual):
    if len(reference) != len(actual):
        return {"reference_samples": len(reference), "faar_samples": len(actual),
                "count_match": False}
    max_error = 0.0
    diff_energy = ref_energy = 0.0
    for ref, got in zip(reference, actual):
        if not math.isfinite(ref) or not math.isfinite(got):
            raise RuntimeError("non-finite output sample")
        error = got - ref
        max_error = max(max_error, abs(error))
        diff_energy += error * error
        ref_energy += ref * ref
    rms_error = math.sqrt(diff_energy / max(1, len(reference)))
    ref_rms = math.sqrt(ref_energy / max(1, len(reference)))
    return {"reference_samples": len(reference), "faar_samples": len(actual),
            "count_match": True, "max_abs_error": max_error,
            "rms_error": rms_error, "reference_rms": ref_rms,
            "error_dbfs": 20 * math.log10(rms_error) if rms_error else None}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--faar", required=True, help="path to the faar CLI")
    parser.add_argument("--sox", required=True, help="path to the SoX Vulkan CLI")
    parser.add_argument("--profiles", nargs="+",
                        choices=("cpu", "fast", "precise", "reference"),
                        default=("cpu", "fast", "precise", "reference"),
                        help="SoX CPU path and/or Vulkan numerical profiles")
    parser.add_argument("--input-rate", type=int, default=96000)
    parser.add_argument("--output-rate", type=int, default=44100)
    parser.add_argument("--channels", type=int, default=2)
    parser.add_argument("--seconds", type=int, default=5)
    parser.add_argument("--buffer", type=int, default=262144)
    parser.add_argument("--warmups", type=int, default=1)
    parser.add_argument("--repeats", type=int, default=7)
    parser.add_argument("--work-dir", help="retain raw inputs and outputs here")
    args = parser.parse_args()
    if not (1 <= args.channels <= 8 and args.seconds > 0 and args.buffer > 0
            and args.warmups >= 0 and args.repeats > 0):
        parser.error("channels must be 1..8; seconds, buffer, repeats positive and warmups nonnegative")
    if args.input_rate <= 0 or args.output_rate <= 0:
        parser.error("sample rates must be positive")

    owned = args.work_dir is None
    tmp = tempfile.TemporaryDirectory(prefix="faar-sox-vulkan-") if owned else None
    root = Path(tmp.name if tmp else args.work_dir)
    root.mkdir(parents=True, exist_ok=True)
    frames = args.input_rate * args.seconds
    sox_input, faar_input = root / "sox-input.f32", root / "faar-input.f32"
    digest = input_files(frames, args.input_rate, args.channels, sox_input, faar_input)
    faar_output = root / "faar-output.f32"
    faar_command = [args.faar, "--rate", str(args.output_rate), "--in-rate",
                    str(args.input_rate), "--channels", str(args.channels),
                    "--format", "f32le", "--quality", "high", "-v0",
                    str(faar_input), str(faar_output)]
    for _ in range(args.warmups):
        run(faar_command)
    faar_runs = [run(faar_command) for _ in range(args.repeats)]
    faar_samples = read_f32(faar_output)
    want = expected_frames(frames, args.input_rate, args.output_rate) * args.channels
    if len(faar_samples) != want:
        raise RuntimeError("FAAR count %d differs from expected %d" % (len(faar_samples), want))

    report = {"input_sha256": digest, "frames_per_channel": frames,
              "input_rate": args.input_rate, "output_rate": args.output_rate,
              "channels": args.channels,
              "warmups": args.warmups, "repeats": args.repeats,
              "faar": {"median": timing_summary(faar_runs), "runs": faar_runs},
              "profiles": {}}
    for profile in args.profiles:
        backend = "cpu" if profile == "cpu" else "vulkan-" + profile
        output = root / ("sox-%s-output.f32" % backend)
        command = [args.sox]
        if profile != "cpu":
            command.append("--vulkan-" + profile)
        command += ["--single-threaded",
                   "--buffer", str(args.buffer), "--input-buffer", str(args.buffer),
                   "-t", "raw", "-e", "float", "-b", "32", "-L",
                   "-r", str(args.input_rate), "-c", str(args.channels),
                   str(sox_input), "-t", "raw", "-e", "float", "-b", "32", "-L",
                   "-r", str(args.output_rate), "-c", str(args.channels),
                   str(output), "rate", "-v", str(args.output_rate)]
        for _ in range(args.warmups):
            run(command)
        timing_runs = [run(command) for _ in range(args.repeats)]
        ref = read_f32(output)
        metrics = compare(ref, faar_samples)
        metrics["timing"] = {"median": timing_summary(timing_runs),
                              "runs": timing_runs}
        metrics["command"] = command
        report["profiles"][backend] = metrics
        if len(ref) != want:
            raise RuntimeError("SoX %s count %d differs from expected %d" %
                               (profile, len(ref), want))
    report["working_directory"] = str(root) if not owned else "temporary"
    print(json.dumps(report, indent=2, sort_keys=True))
    if tmp:
        tmp.cleanup()


if __name__ == "__main__":
    main()
