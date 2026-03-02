import subprocess
import time
import os
import wave
import struct
import math
import hashlib

def generate_test_wav(filename, duration=10, sample_rate=44100, type='complex', seed=42):
    num_samples = int(duration * sample_rate)
    with wave.open(filename, 'w') as wav_file:
        wav_file.setnchannels(2)
        wav_file.setsampwidth(2)
        wav_file.setframerate(sample_rate)
        for i in range(num_samples):
            if type == 'complex':
                # Complex signal: sine sweep + occasional noise bursts
                freq = 440 + 5000 * (i / num_samples)
                value = int(16384.0 * math.sin(2.0 * math.pi * freq * i / sample_rate))
                if (i // (sample_rate // 2)) % 4 == 0:
                    value += int(8000 * (2 * (i % 2) - 1))
            elif type == 'transient':
                # Clicks
                if i % (sample_rate // 2) < 100:
                    value = 20000 if (i % 2 == 0) else -20000
                else:
                    value = 0
            elif type == 'noise':
                # White noise
                import random
                random.seed(seed + i)
                value = int(random.uniform(-16384, 16383))
            elif type == 'sine':
                value = int(16384.0 * math.sin(2.0 * math.pi * 1000 * i / sample_rate))
            else:
                value = 0

            packed = struct.pack('<hh', max(-32768, min(32767, int(value))), max(-32768, min(32767, int(value))))
            wav_file.writeframes(packed)

def get_md5(filename):
    hash_md5 = hashlib.md5()
    with open(filename, "rb") as f:
        for chunk in iter(lambda: f.read(4096), b""):
            hash_md5.update(chunk)
    return hash_md5.hexdigest()

def run_bench(faac_path, args):
    start = time.time()
    result = subprocess.run([faac_path] + args, capture_output=True, text=True)
    end = time.time()
    return end - start, result.stderr

def main():
    faac_path = './build/frontend/faac'
    if not os.path.exists(faac_path):
        print(f"Error: {faac_path} not found. Build the project first.")
        return

    test_signals = ['complex', 'transient', 'noise', 'sine']
    sample_rates = [16000, 44100]
    psy_models = [0, 1] # 0 = knipsycho (current), 1 = MDCT-based (to be implemented)

    print(f"{'Signal':<10} | {'Rate':<6} | {'Psy':<3} | {'Time (s)':<10} | {'Size':<10} | {'MD5'}")
    print("-" * 70)

    for sig in test_signals:
        for rate in sample_rates:
            wav_file = f'test_{sig}_{rate}.wav'
            generate_test_wav(wav_file, type=sig, sample_rate=rate)

            for psy in psy_models:
                out_aac = f'out_{sig}_{rate}_psy{psy}.aac'
                # Assuming --psy <n> is the flag to select psychoacoustic model
                args = ['-b', '64', '--psy', str(psy), wav_file, '-o', out_aac]

                try:
                    duration, stderr = run_bench(faac_path, args)
                    if os.path.exists(out_aac):
                        size = os.path.getsize(out_aac)
                        md5 = get_md5(out_aac)
                        print(f"{sig:<10} | {rate:<6} | {psy:<3} | {duration:<10.3f} | {size:<10} | {md5}")
                    else:
                        print(f"{sig:<10} | {rate:<6} | {psy:<3} | FAILED     | -          | -")
                except Exception as e:
                    print(f"{sig:<10} | {rate:<6} | {psy:<3} | ERROR      | {str(e)}")
                finally:
                    if os.path.exists(out_aac):
                        os.remove(out_aac)

            if os.path.exists(wav_file):
                os.remove(wav_file)

if __name__ == "__main__":
    main()
