#!/usr/bin/env python3
"""Compare float and mixed libfaar builds on one host."""

import argparse
import csv
import json
import statistics
import subprocess
import sys
from pathlib import Path


MODES = ("float", "mixed")
ROUTES = ((48000, 44100), (96000, 44100), (192000, 44100))
CHANNELS = (1, 2, 8)


def run(executable, arguments):
    result = subprocess.check_output([str(executable), *map(str, arguments)], text=True)
    return [field.strip() for field in result.strip().split(",")]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--float-build", type=Path, default=Path("build-faar-float"))
    parser.add_argument("--mixed-build", type=Path, default=Path("build-faar-mixed"))
    parser.add_argument("--repeats", type=int, default=7)
    parser.add_argument("--prepare-frames", type=int, default=8192)
    parser.add_argument("--input-block", type=int, default=1024)
    parser.add_argument("--output-capacity", type=int, default=1024)
    args = parser.parse_args()
    if (args.repeats < 1 or args.prepare_frames < 0
            or args.input_block < 1 or args.output_capacity < 1):
        parser.error("repeats, input-block, and output-capacity must be positive; "
                     "prepare-frames must be nonnegative")

    builds = {
        "float": args.float_build,
        "mixed": args.mixed_build,
    }
    benchmarks = {
        mode: {
            "speed": path / "libfaar/tests/bench_faar",
            "heap": path / "libfaar/tests/bench_faar_heap",
        }
        for mode, path in builds.items()
    }
    missing = [str(exe) for pair in benchmarks.values() for exe in pair.values()
               if not exe.is_file()]
    if missing:
        parser.error("build these targets first: " + ", ".join(missing))

    for mode, path in builds.items():
        try:
            options = json.loads(subprocess.check_output(
                ["meson", "introspect", "--buildoptions", str(path)], text=True))
        except (OSError, subprocess.CalledProcessError, json.JSONDecodeError) as exc:
            parser.error(f"cannot inspect Meson build {path}: {exc}")
        values = {option["name"]: option["value"] for option in options}
        if values.get("faar-precision") != mode:
            parser.error(f"{path} is not configured with faar-precision={mode}")
        if not values.get("b_lto"):
            parser.error(f"{path} must be configured with -Db_lto=true")

    writer = csv.writer(sys.stdout, lineterminator="\n")
    writer.writerow(("kind", "in_rate", "out_rate", "channels", "quality",
                     "float", "mixed", "units"))
    for in_rate, out_rate in ROUTES:
        for channels in CHANNELS:
            for quality in range(3):
                samples = {mode: [] for mode in MODES}
                for repeat in range(args.repeats):
                    rotate = repeat % len(MODES)
                    order = MODES[rotate:] + MODES[:rotate]
                    for mode in order:
                        row = run(benchmarks[mode]["speed"],
                                  (in_rate, out_rate, channels, quality,
                                   args.input_block, args.output_capacity))
                        samples[mode].append(float(row[7]))
                writer.writerow(("throughput", in_rate, out_rate, channels,
                                 quality, *(f"{statistics.median(samples[m]):.3f}"
                                            for m in MODES), "Mframes/s"))

                peaks = {}
                for mode in MODES:
                    row = run(benchmarks[mode]["heap"],
                              (in_rate, out_rate, channels, quality,
                               args.prepare_frames))
                    if int(row[9]) != 0:
                        raise RuntimeError(f"{mode} leaked {row[9]} bytes after close")
                    peaks[mode] = int(row[8])
                writer.writerow(("prepared_peak_heap", in_rate, out_rate, channels,
                                 quality, *(peaks[m] for m in MODES), "bytes"))


if __name__ == "__main__":
    main()
