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

        buffer_size = sample_rate # 1 second chunks

        if func_name == "sine":
            freq = 1000.0
            factor = 2.0 * math.pi * freq / sample_rate
            for i in range(0, num_samples, buffer_size):
                chunk_size = min(buffer_size, num_samples - i)
                data = []
                for j in range(i, i + chunk_size):
                    val = math.sin(factor * j)
                    ival = max(-32768, min(32767, int(val * 32767)))
                    data.append(struct.pack('<hh', ival, ival))
                wav_file.writeframesraw(b"".join(data))

        elif func_name == "sweep":
            f0 = 20.0
            f1 = 20000.0
            duration = float(duration_sec)
            for i in range(0, num_samples, buffer_size):
                chunk_size = min(buffer_size, num_samples - i)
                data = []
                for j in range(i, i + chunk_size):
                    t = j / sample_rate
                    freq = f0 + (f1 - f0) * (t / duration)
                    val = math.sin(2.0 * math.pi * freq * t)
                    ival = max(-32768, min(32767, int(val * 32767)))
                    data.append(struct.pack('<hh', ival, ival))
                wav_file.writeframesraw(b"".join(data))

        elif func_name == "noise":
            random.seed(42)
            for i in range(0, num_samples, buffer_size):
                chunk_size = min(buffer_size, num_samples - i)
                data = []
                for j in range(i, i + chunk_size):
                    val = random.uniform(-1, 1)
                    ival = max(-32768, min(32767, int(val * 32767)))
                    data.append(struct.pack('<hh', ival, ival))
                wav_file.writeframesraw(b"".join(data))

        elif func_name == "harpsichord":
            # Impulsive harmonics
            random.seed(42)
            for i in range(0, num_samples, buffer_size):
                chunk_size = min(buffer_size, num_samples - i)
                data = []
                for j in range(i, i + chunk_size):
                    t = j / sample_rate
                    # Pulse every 0.5s
                    env = math.exp(-10 * (t % 0.5))
                    val = 0
                    for harm in [1, 2, 3, 5, 8]:
                        val += math.sin(2.0 * math.pi * 440 * harm * t) * (1.0/harm)
                    val *= env
                    ival = max(-32768, min(32767, int(val * 16384)))
                    data.append(struct.pack('<hh', ival, ival))
                wav_file.writeframesraw(b"".join(data))

        elif func_name == "castanets":
            # Sharp transients
            random.seed(42)
            for i in range(0, num_samples, buffer_size):
                chunk_size = min(buffer_size, num_samples - i)
                data = []
                for j in range(i, i + chunk_size):
                    t = j / sample_rate
                    # Click every 0.25s
                    pos = t % 0.25
                    env = math.exp(-100 * pos) if pos < 0.05 else 0
                    val = random.uniform(-1, 1) * env
                    ival = max(-32768, min(32767, int(val * 32767)))
                    data.append(struct.pack('<hh', ival, ival))
                wav_file.writeframesraw(b"".join(data))

        elif func_name == "applause":
            # Dense modulated noise
            random.seed(42)
            for i in range(0, num_samples, buffer_size):
                chunk_size = min(buffer_size, num_samples - i)
                data = []
                for j in range(i, i + chunk_size):
                    t = j / sample_rate
                    # Multiple overlapping noise pulses
                    val = 0
                    for p in range(5):
                        env = math.exp(-20 * ((t + p*0.07) % 0.3))
                        val += random.uniform(-1, 1) * env
                    val *= 0.4
                    ival = max(-32768, min(32767, int(val * 32767)))
                    data.append(struct.pack('<hh', ival, ival))
                wav_file.writeframesraw(b"".join(data))

        elif func_name == "silence":
            data = struct.pack('<hh', 0, 0) * buffer_size
            for i in range(0, num_samples, buffer_size):
                chunk_size = min(buffer_size, num_samples - i)
                if chunk_size < buffer_size:
                    wav_file.writeframesraw(struct.pack('<hh', 0, 0) * chunk_size)
                else:
                    wav_file.writeframesraw(data)

if __name__ == "__main__":
    rates = [16000, 44100, 48000]
    duration = 600 # 10 minutes

    for rate in rates:
        print(f"Generating {rate}Hz suite...")
        for t in ["sine", "sweep", "noise", "harpsichord", "castanets", "applause", "silence"]:
            generate_wave(f"tests/data/{t}_{rate}.wav", duration, rate, t)
    print("Done.")
