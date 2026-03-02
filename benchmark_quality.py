import subprocess
import time
import os
import wave
import struct
import math

def generate_test_wav(filename, duration=10, sample_rate=16000, type='complex'):
    num_samples = int(duration * sample_rate)
    with wave.open(filename, 'w') as wav_file:
        wav_file.setnchannels(1)
        wav_file.setsampwidth(2)
        wav_file.setframerate(sample_rate)
        for i in range(num_samples):
            if type == 'complex':
                # Complex signal: sine sweep + occasional noise bursts
                freq = 440 + 2000 * (i / num_samples)
                value = int(16384.0 * math.sin(2.0 * math.pi * freq * i / sample_rate))
                if (i // (sample_rate // 2)) % 4 == 0:
                    value += int(8000 * (2 * (i % 2) - 1))
            elif type == 'transient':
                # Clicks
                if i % (sample_rate // 2) < 10:
                    value = 32767 if (i % 2 == 0) else -32768
                else:
                    value = 0
            else:
                value = int(16384.0 * math.sin(2.0 * math.pi * 1000 * i / sample_rate))

            wav_file.writeframes(struct.pack('<h', max(-32768, min(32767, int(value)))))

def run_bench(args):
    start = time.time()
    result = subprocess.run(['./build/frontend/faac'] + args, capture_output=True, text=True)
    end = time.time()
    return end - start, result.stderr

def main():
    for test_type in ['complex', 'transient']:
        wav_file = f'bench_{test_type}.wav'
        print(f"\nTesting {test_type} signal...")
        generate_test_wav(wav_file, type=test_type)

        configs = [
            ("New (Default)", []),
            ("New (Level 10)", ["--spreading", "10", "--tns-short", "10"]),
            ("Old (All Off)", ["--no-bit-reservoir", "--spreading", "0", "--tns-short", "0"]),
            ("No Reservoir", ["--no-bit-reservoir"]),
            ("No Spreading", ["--spreading", "0"]),
            ("No TNS Short", ["--tns-short", "0"]),
        ]

        bitrate = "32"
        print(f"Bitrate: {bitrate}kbps")
        print(f"{'Configuration':<20} | {'Time (s)':<10} | {'Output Size':<12}")
        print("-" * 50)

        for name, extra_args in configs:
            out_aac = f"out_{name.replace(' ', '_').lower()}.aac"
            args = ["-b", bitrate, wav_file, "-o", out_aac] + extra_args
            duration, stderr = run_bench(args)
            size = os.path.getsize(out_aac)
            print(f"{name:<20} | {duration:<10.3f} | {size:<12}")
            if os.path.exists(out_aac):
                os.remove(out_aac)

        if os.path.exists(wav_file):
            os.remove(wav_file)

if __name__ == "__main__":
    main()
