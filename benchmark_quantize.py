import subprocess
import time
import hashlib
import os
import sys
import statistics

def get_md5(filename):
    hash_md5 = hashlib.md5()
    with open(filename, "rb") as f:
        for chunk in iter(lambda: f.read(4096), b""):
            hash_md5.update(chunk)
    return hash_md5.hexdigest()

def run_bench(precision, opt_mode):
    build_dir = f"build_{precision}_{opt_mode}"
    if not os.path.exists(build_dir):
        cmd = ["meson", "setup", build_dir, f"-Dfloating-point={precision}", "-Dbuildtype=release"]
        if opt_mode == 'scalar':
            cmd += ["-Dsse2=false", "-Davx2=false"]
        elif opt_mode == 'sse2':
            cmd += ["-Dsse2=true", "-Davx2=false"]
        elif opt_mode == 'avx2':
            cmd += ["-Dsse2=true", "-Davx2=true"]

        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0:
            print(f"Meson setup failed for {precision} {opt_mode}")
            return None

    res = subprocess.run(["ninja", "-C", build_dir], capture_output=True, text=True)
    if res.returncode != 0:
        print(f"Ninja build failed for {precision} {opt_mode}")
        return None

    faac_bin = f"./{build_dir}/frontend/faac"
    test_waves = ["test_waves/sine.wav", "test_waves/sweep.wav", "test_waves/noise.wav", "test_waves/silence.wav"]

    results = {}
    num_runs = 5
    for wave in test_waves:
        out_aac = f"{wave}_{precision}_{opt_mode}.aac"

        times = []
        for i in range(num_runs):
            start_time = time.time()
            subprocess.run([faac_bin, wave, "-o", out_aac], capture_output=True, text=True)
            end_time = time.time()
            times.append(end_time - start_time)

        results[wave] = {
            "times": times,
            "mean": statistics.mean(times),
            "stdev": statistics.stdev(times) if len(times) > 1 else 0,
            "min": min(times),
            "md5": get_md5(out_aac)
        }

    return results

if __name__ == "__main__":
    precisions = ["single", "double"]
    modes = ["scalar", "sse2", "avx2"]

    all_results = {}
    for prec in precisions:
        print(f"Benchmarking {prec} precision (5 runs each):")
        all_results[prec] = {}
        for mode in modes:
            res = run_bench(prec, mode)
            if res:
                all_results[prec][mode] = res
                total_min = sum(r['min'] for r in res.values())
                print(f" Mode: {mode:7} | Total Min Time: {total_min:6.2f}s")

    print("\nSummary (Relative to Scalar):")
    for prec in precisions:
        print(f" {prec}:")
        if 'scalar' not in all_results[prec]: continue
        scalar_min = sum(r['min'] for r in all_results[prec]['scalar'].values())
        for mode in ['sse2', 'avx2']:
            if mode not in all_results[prec]: continue
            mode_min = sum(r['min'] for r in all_results[prec][mode].values())
            speedup = scalar_min / mode_min

            mismatched = []
            for wave in all_results[prec]['scalar']:
                if all_results[prec][mode][wave]['md5'] != all_results[prec]['scalar'][wave]['md5']:
                    mismatched.append(wave)

            status = "MATCH" if not mismatched else "MISMATCH"
            if status == "MISMATCH" and mode == "sse2" and prec == "double":
                status = "MISMATCH (Expected legacy)"

            print(f"  {mode:5}: Speedup: {speedup:5.2f}x | MD5: {status}")
            # Also print stdev info for one wave to show validity
            example_wave = "test_waves/sine.wav"
            stdev = all_results[prec][mode][example_wave]['stdev']
            mean = all_results[prec][mode][example_wave]['mean']
            print(f"    (Sine wave: mean={mean:.2f}s, stdev={stdev:.4f}s)")
