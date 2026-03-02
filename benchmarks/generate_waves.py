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

        # Buffering for speed
        buffer_size = 44100

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
                    # Exponential sweep is usually better
                    # freq = f0 * (f1 / f0) ** (t / duration)
                    # Linear sweep as requested before
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

        elif func_name == "silence":
            data = struct.pack('<hh', 0, 0) * buffer_size
            for i in range(0, num_samples, buffer_size):
                chunk_size = min(buffer_size, num_samples - i)
                if chunk_size < buffer_size:
                    wav_file.writeframesraw(struct.pack('<hh', 0, 0) * chunk_size)
                else:
                    wav_file.writeframesraw(data)

if __name__ == "__main__":
    sample_rate = 44100
    duration = 600 # 10 minutes

    print("Generating sine wave...")
    generate_wave("benchmarks/data/sine.wav", duration, sample_rate, "sine")
    print("Generating sweep...")
    generate_wave("benchmarks/data/sweep.wav", duration, sample_rate, "sweep")
    print("Generating noise...")
    generate_wave("benchmarks/data/noise.wav", duration, sample_rate, "noise")
    print("Generating silence...")
    generate_wave("benchmarks/data/silence.wav", duration, sample_rate, "silence")
    print("Done.")
