#!/usr/bin/env python3
"""Compare full FAAD CLI throughput, verifying output before paired timings.

Example: python3 tests/faad_cli_ab.py --baseline /path/to/old/faad \
    --candidate build/frontend/faad --rounds 12 --json /tmp/faad-ab.json clips/*.m4a
File output uses a temporary directory; stdout output is redirected to the
null device. Neither mode includes Python pipe-reading overhead in timings.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import statistics
import subprocess
import tempfile
import time


def identity(path):
    path = Path(path).resolve()
    return {"path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def command(binary, source, output, mode, bits="16", fmt="wav", extra=()):
    args = [binary, "-q", "--overwrite", "-b", bits, "-f", fmt, *extra]
    args += ["-o", str(output)] if mode == "file" else ["-w"]
    return [*args, str(source)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", required=True)
    parser.add_argument("--candidate", required=True)
    parser.add_argument("--control", help="Optional identical baseline binary to measure noise")
    parser.add_argument("--rounds", type=int, default=12)
    parser.add_argument("--json", type=Path, required=True)
    parser.add_argument("inputs", nargs="+", type=Path)
    args = parser.parse_args()
    if args.rounds < 2:
        parser.error("--rounds must be at least 2")
    binaries = [str(Path(p).resolve()) for p in (args.baseline, args.candidate)]
    if args.control:
        binaries.append(str(Path(args.control).resolve()))
    report = {"platform": platform.platform(), "binaries": [identity(p) for p in binaries],
              "rounds": args.rounds, "results": []}
    with tempfile.TemporaryDirectory(prefix="faad-cli-ab-") as directory:
        outputs = [Path(directory) / f"{i}.pcm" for i in range(len(binaries))]
        for source in args.inputs:
            source = source.resolve()
            # Require identical bytes and successful exits for both file and
            # stdout, all depths/containers, gapless disabled, and seeking.
            gates = []
            for bits in ("16", "24", "32", "32f"):
                for fmt in ("wav", "raw"):
                    for mode in ("file", "stdout"):
                        hashes = []
                        for index, binary in enumerate(binaries):
                            cmd = command(binary, source, outputs[index], mode, bits, fmt)
                            if mode == "stdout":
                                with outputs[index].open("wb") as sink:
                                    subprocess.run(cmd, stdout=sink, stderr=subprocess.PIPE, check=True)
                            else:
                                subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, check=True)
                            hashes.append(identity(outputs[index])["sha256"])
                        if any(value != hashes[0] for value in hashes[1:]):
                            raise RuntimeError(f"Output mismatch: {source}, {mode}, {bits}, {fmt}")
                        gates.append({"mode": mode, "bits": bits, "format": fmt, "sha256": hashes[0]})
            for extra in (("-g",), ("-j", "0.25")):
                for index, binary in enumerate(binaries):
                    subprocess.run(command(binary, source, outputs[index], "file", extra=extra),
                                   stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, check=True)
                if any(output.read_bytes() != outputs[0].read_bytes() for output in outputs[1:]):
                    raise RuntimeError(f"Output mismatch: {source}, {extra}")
            for mode in ("file", "stdout"):
                samples = [[] for _ in binaries]
                cmds = [command(binary, source, outputs[index], mode) for index, binary in enumerate(binaries)]
                for cmd in cmds:
                    subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, check=True)
                for round_index in range(args.rounds):
                    # Rotate order to balance drift; serial, never concurrent.
                    order = list(range(len(binaries)))
                    offset = round_index % len(order)
                    for index in order[offset:] + order[:offset]:
                        start = time.perf_counter_ns()
                        subprocess.run(cmds[index], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, check=True)
                        samples[index].append((time.perf_counter_ns() - start) / 1e6)
                gains = [(old / new - 1) * 100 for old, new in zip(samples[0], samples[1])]
                result = {"input": identity(source), "mode": mode, "commands": cmds,
                          "exactness": gates, "samples_ms": samples,
                          "median_ms": [statistics.median(s) for s in samples],
                          "paired_speedup_pct": gains, "median_speedup_pct": statistics.median(gains)}
                if args.control:
                    control_gains = [(old / new - 1) * 100 for old, new in zip(samples[0], samples[2])]
                    result["control_paired_speedup_pct"] = control_gains
                    result["control_median_speedup_pct"] = statistics.median(control_gains)
                report["results"].append(result)
                args.json.write_text(json.dumps(report, indent=2) + "\n")
                print(f"{source.name} {mode}: {result['median_ms'][0]:.2f} -> "
                      f"{result['median_ms'][1]:.2f} ms; paired {result['median_speedup_pct']:+.2f}%", flush=True)


if __name__ == "__main__":
    main()
