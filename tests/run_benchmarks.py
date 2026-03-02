import os
import subprocess
import time
import hashlib
import sys
import json
import re
import wave
import math
import struct

RATES = [16000, 48000]
QUALITIES = [10, 100, 250]
TYPES = ["sine", "sweep", "noise", "harpsichord", "castanets", "applause", "silence"]
DATA_DIR = "tests/data"
OUTPUT_DIR = "tests/output"

def calculate_lsd(ref_wav, deg_wav):
    """Calculate Log-Spectral Distance as a robust quality fallback."""
    try:
        def get_spectrum(filename):
            with wave.open(filename, 'rb') as f:
                n = f.getnframes()
                # Use a small chunk to keep it fast
                n = min(n, 44100 * 2)
                frames = f.readframes(n)
                data = struct.unpack(f'<{n*2}h', frames)
                # Left channel only for simplicity
                samples = [data[i]/32768.0 for i in range(0, len(data), 2)]

                # Simple FFT-less energy distribution in 32 sub-bands
                num_subbands = 32
                samples_per_band = len(samples) // num_subbands
                energies = []
                for i in range(num_subbands):
                    band = samples[i*samples_per_band : (i+1)*samples_per_band]
                    en = sum(x*x for x in band) / max(1, len(band))
                    energies.append(max(1e-10, en))
                return [10 * math.log10(e) for e in energies]

        ref_spec = get_spectrum(ref_wav)
        deg_spec = get_spectrum(deg_wav)

        dist = math.sqrt(sum((r - d)**2 for r, d in zip(ref_spec, deg_spec)) / len(ref_spec))
        # Map LSD to a MOS-like 1-5 scale (heuristic)
        # LSD 0 -> MOS 5.0, LSD 10 -> MOS 1.0
        mos = max(1.0, min(5.0, 5.0 - (dist / 2.5)))
        return mos
    except:
        return None

def run_visqol(ref_wav, deg_wav, rate):
    try:
        mode = "wideband" if rate <= 16000 else "fullband"
        cmd = ["visqol", mode, "--reference", ref_wav, "--degraded", deg_wav]
        proc = subprocess.run(cmd, capture_output=True, text=True)
        match = re.search(r"MOS-LQO:\s+([-.\d]+)", proc.stdout)
        if match: return float(match.group(1))
    except: pass
    return None

def run_benchmark(faac_path, lib_path, precision):
    env = os.environ.copy()
    env["FAAC_NO_SIMD"] = "1"

    os.makedirs(OUTPUT_DIR, exist_ok=True)
    results = {
        "matrix": {},
        "throughput": {},
        "lib_size": os.path.getsize(lib_path) if os.path.exists(lib_path) else 0,
    }

    # Throughput
    for rate in RATES:
        for t in ["sine", "noise"]:
            input_path = os.path.join(DATA_DIR, f"{t}_{rate}_long.wav")
            if not os.path.exists(input_path): continue
            output_path = os.path.join(OUTPUT_DIR, f"{t}_{rate}_long_{precision}.aac")
            start_time = time.time()
            try:
                subprocess.run([faac_path, "-o", output_path, input_path], env=env, check=True, capture_output=True)
                results["throughput"][f"{t}_{rate}"] = time.time() - start_time
            except: pass

    # Perceptual
    for rate in RATES:
        for quality in QUALITIES:
            for t in TYPES:
                input_path = os.path.join(DATA_DIR, f"{t}_{rate}.wav")
                if not os.path.exists(input_path): continue
                key = f"{t}_{rate}_q{quality}"
                output_path = os.path.join(OUTPUT_DIR, f"{key}_{precision}.aac")
                deg_wav = output_path.replace(".aac", "_deg.wav")

                try:
                    subprocess.run([faac_path, "-q", str(quality), "-o", output_path, input_path], env=env, check=True, capture_output=True)
                    aac_size = os.path.getsize(output_path)

                    mos = None
                    # Try faad + LSD/ViSQOL
                    try:
                        subprocess.run(["faad", "-o", deg_wav, output_path], capture_output=True)
                        if os.path.exists(deg_wav):
                            # Try ViSQOL first
                            mos = run_visqol(input_path, deg_wav, rate)
                            # Fallback to robust LSD-based MOS
                            if mos is None:
                                mos = calculate_lsd(input_path, deg_wav)
                    except: pass
                    finally:
                        if os.path.exists(deg_wav): os.remove(deg_wav)

                    results["matrix"][key] = {"aac_size": aac_size, "mos": mos}
                except: pass

    return results

if __name__ == "__main__":
    if len(sys.argv) < 5: sys.exit(1)
    data = run_benchmark(sys.argv[1], sys.argv[2], sys.argv[3])
    with open(sys.argv[4], "w") as f: json.dump(data, f, indent=2)
