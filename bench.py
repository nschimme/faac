import subprocess
import time
import os
import hashlib
import json

def get_md5(filename):
    hash_md5 = hashlib.md5()
    with open(filename, "rb") as f:
        for chunk in iter(lambda: f.read(4096), b""):
            hash_md5.update(chunk)
    return hash_md5.hexdigest()

def run_bench(precision, sse_enabled):
    print(f"Benchmarking precision={precision}, SSE2={sse_enabled}")

    build_dir = f"build_{precision}_{'sse' if sse_enabled else 'scalar'}"
    if os.path.exists(build_dir):
        subprocess.run(["rm", "-rf", build_dir], check=True)

    cmd = [
        "meson", "setup", build_dir,
        f"-Dfloating-point={precision}",
        f"-Dsse2={'true' if sse_enabled else 'false'}",
        "-Dc_args=-O3 -march=native"
    ]

    subprocess.run(cmd, check=True, capture_output=True)
    subprocess.run(["ninja", "-C", build_dir], check=True, capture_output=True)

    faac_bin = os.path.join(build_dir, "frontend", "faac")

    files = ["sine.wav", "sweep.wav", "noise.wav", "silence.wav"]
    results = {}

    for f in files:
        out_aac = f"{f}_{precision}_{'sse' if sse_enabled else 'scalar'}.aac"
        start_time = time.time()
        # default encoder parameters
        subprocess.run([faac_bin, f, "-o", out_aac], check=True, capture_output=True)
        end_time = time.time()

        results[f] = {
            "time": end_time - start_time,
            "md5": get_md5(out_aac)
        }
        print(f"  {f}: {results[f]['time']:.2f}s, {results[f]['md5']}")

    return results

if __name__ == "__main__":
    res = {}
    res["single_sse"] = run_bench("single", True)
    res["double_sse"] = run_bench("double", True)
    res["single_scalar"] = run_bench("single", False)
    res["double_scalar"] = run_bench("double", False)

    with open("results.json", "w") as f:
        json.dump(res, f, indent=4)

    print("Results saved to results.json")
