import wave
import math
import struct
import os
import random

def generate_wave(filename, duration_sec, sample_rate, func_name):
    num_samples = int(duration_sec * sample_rate)
    os.makedirs(os.path.dirname(filename), exist_ok=True)

    with wave.open(filename, 'wb') as wav_file:
        wav_file.setnchannels(2)  # Stereo
        wav_file.setsampwidth(2)  # 16-bit
        wav_file.setframerate(sample_rate)

        buffer_size = min(num_samples, sample_rate)

        # ViSQOL Best Practice: Normalization to -3 dBFS (0.7 amplitude)
        AMP = 0.7

        if func_name == "sine":
            freq = 1000.0
            factor = 2.0 * math.pi * freq / sample_rate
            for i in range(0, num_samples, buffer_size):
                chunk_size = min(buffer_size, num_samples - i)
                data = []
                for j in range(i, i + chunk_size):
                    val = math.sin(factor * j) * AMP
                    ival = max(-32768, min(32767, int(val * 32767)))
                    data.append(struct.pack('<hh', ival, ival))
                wav_file.writeframesraw(b"".join(data))

        elif func_name == "sweep":
            f0 = 20.0
            f1 = min(20000.0, sample_rate / 2 - 100)
            duration = float(duration_sec)
            for i in range(0, num_samples, buffer_size):
                chunk_size = min(buffer_size, num_samples - i)
                data = []
                for j in range(i, i + chunk_size):
                    t = j / sample_rate
                    freq = f0 + (f1 - f0) * (t / duration)
                    val = math.sin(2.0 * math.pi * freq * t) * AMP
                    ival = max(-32768, min(32767, int(val * 32767)))
                    data.append(struct.pack('<hh', ival, ival))
                wav_file.writeframesraw(b"".join(data))

        elif func_name == "noise":
            random.seed(42)
            for i in range(0, num_samples, buffer_size):
                chunk_size = min(buffer_size, num_samples - i)
                data = []
                for j in range(i, i + chunk_size):
                    val = random.uniform(-1, 1) * AMP
                    ival = max(-32768, min(32767, int(val * 32767)))
                    data.append(struct.pack('<hh', ival, ival))
                wav_file.writeframesraw(b"".join(data))

        elif func_name == "harpsichord":
            random.seed(42)
            for i in range(0, num_samples, buffer_size):
                chunk_size = min(buffer_size, num_samples - i)
                data = []
                for j in range(i, i + chunk_size):
                    t = j / sample_rate
                    env = math.exp(-10 * (t % 0.5))
                    val = 0
                    for harm in [1, 2, 3, 5, 8]:
                        val += math.sin(2.0 * math.pi * 440 * harm * t) * (1.0/harm)
                    val *= env * AMP
                    ival = max(-32768, min(32767, int(val * 16384)))
                    data.append(struct.pack('<hh', ival, ival))
                wav_file.writeframesraw(b"".join(data))

        elif func_name == "castanets":
            random.seed(42)
            for i in range(0, num_samples, buffer_size):
                chunk_size = min(buffer_size, num_samples - i)
                data = []
                for j in range(i, i + chunk_size):
                    t = j / sample_rate
                    pos = t % 0.25
                    env = math.exp(-100 * pos) if pos < 0.05 else 0
                    val = random.uniform(-1, 1) * env * AMP
                    ival = max(-32768, min(32767, int(val * 32767)))
                    data.append(struct.pack('<hh', ival, ival))
                wav_file.writeframesraw(b"".join(data))

        elif func_name == "applause":
            random.seed(42)
            for i in range(0, num_samples, buffer_size):
                chunk_size = min(buffer_size, num_samples - i)
                data = []
                for j in range(i, i + chunk_size):
                    t = j / sample_rate
                    val = 0
                    for p in range(5):
                        env = math.exp(-20 * ((t + p*0.07) % 0.3))
                        val += random.uniform(-1, 1) * env
                    val *= 0.4 * AMP
                    ival = max(-32768, min(32767, int(val * 32767)))
                    data.append(struct.pack('<hh', ival, ival))
                wav_file.writeframesraw(b"".join(data))

        elif func_name == "silence":
            # Very low noise floor instead of pure digital zero for some metric stability
            for i in range(0, num_samples, buffer_size):
                chunk_size = min(buffer_size, num_samples - i)
                data = struct.pack('<hh', 0, 0) * chunk_size
                wav_file.writeframesraw(data)

if __name__ == "__main__":
    # Standard ViSQOL rates: 16k (Wideband) and 48k (Fullband)
    rates = [16000, 48000]
    # Throughput Duration: 120s (2 minutes per combo)
    # Perceptual Duration: 30s (ViSQOL friendly)

    for rate in rates:
        print(f"Generating {rate}Hz suite (Throughput)...")
        for t in ["sine", "noise"]:
            generate_wave(f"tests/data/{t}_{rate}_long.wav", 120, rate, t)

        print(f"Generating {rate}Hz suite (Perceptual)...")
        for t in ["sine", "sweep", "noise", "harpsichord", "castanets", "applause", "silence"]:
            generate_wave(f"tests/data/{t}_{rate}.wav", 30, rate, t)
    print("Done.")
