import os
import subprocess
import time
import math
import wave
import struct
import random
import hashlib

def generate_wav(filename, duration_sec, type='sine', sample_rate=44100):
    print(f"Generating {filename} ({type}, {duration_sec}s)...")
    num_samples = duration_sec * sample_rate
    with wave.open(filename, 'w') as wav_file:
        wav_file.setnchannels(2)
        wav_file.setsampwidth(2)
        wav_file.setframerate(sample_rate)

        random.seed(42)
        for i in range(num_samples):
            if type == 'sine':
                value = int(32767.0 * math.sin(2.0 * math.pi * 440.0 * i / sample_rate))
            elif type == 'sweep':
                freq = 20.0 + (20000.0 - 20.0) * (i / num_samples)
                value = int(32767.0 * math.sin(2.0 * math.pi * freq * i / sample_rate))
            elif type == 'noise':
                value = random.randint(-32768, 32767)
            elif type == 'silence':
                value = 0
            else:
                value = 0

            data = struct.pack('<hh', value, value)
            wav_file.writeframesraw(data)

def get_md5(filename):
    hash_md5 = hashlib.md5()
    with open(filename, "rb") as f:
        for chunk in iter(lambda: f.read(4096), b""):
            hash_md5.update(chunk)
    return hash_md5.hexdigest()

def run_benchmark(faac_path, input_wav, output_aac, runs=5, env=None):
    times = []
    md5 = None

    # Warm up
    subprocess.run([faac_path, "-o", output_aac, input_wav], capture_output=True, env=env)

    for i in range(runs):
        start = time.perf_counter()
        subprocess.run([faac_path, "-o", output_aac, input_wav], capture_output=True, env=env)
        end = time.perf_counter()
        times.append(end - start)

        current_md5 = get_md5(output_aac)
        if md5 is None:
            md5 = current_md5

    avg_time = sum(times) / len(times)
    return {
        'avg': avg_time,
        'md5': md5
    }

if __name__ == "__main__":
    faac_bin = "./build/frontend/faac"
    if not os.path.exists(faac_bin):
        print(f"Error: {faac_bin} not found. Please build the project first.")
        exit(1)

    test_signals = ['sine', 'sweep', 'noise', 'silence']
    duration = 60 # 1 minute

    os.makedirs("test_data", exist_ok=True)
    results = {}

    for signal in test_signals:
        wav_path = f"test_data/{signal}.wav"
        aac_path = f"test_data/{signal}.aac"

        if not os.path.exists(wav_path):
            generate_wav(wav_path, duration, type=signal)

        print(f"Benchmarking {signal}...")

        # SSE2
        env_sse2 = os.environ.copy()
        env_sse2["FAAC_NO_AVX2"] = "1"
        res_sse2 = run_benchmark(faac_bin, wav_path, aac_path, env=env_sse2)

        # AVX2
        res_avx2 = run_benchmark(faac_bin, wav_path, aac_path)

        results[signal] = {
            'sse2': res_sse2,
            'avx2': res_avx2
        }

        speedup = res_sse2['avg'] / res_avx2['avg']
        print(f"  SSE2: {res_sse2['avg']:.4f}s")
        print(f"  AVX2: {res_avx2['avg']:.4f}s (Speedup: {speedup:.4f})")
        if res_sse2['md5'] != res_avx2['md5']:
            print("  MD5 MISMATCH between SSE2 and AVX2!")

    # Print final summary
    print("\nFinal Summary (Average Time):")
    print(f"{'Signal':<10} | {'SSE2':<10} | {'AVX2':<10} | {'Speedup':<10}")
    print("-" * 45)
    for signal in test_signals:
        sse2_avg = results[signal]['sse2']['avg']
        avx2_avg = results[signal]['avx2']['avg']
        speedup = sse2_avg / avx2_avg
        print(f"{signal:<10} | {sse2_avg:<10.4f} | {avx2_avg:<10.4f} | {speedup:<10.4f}")
