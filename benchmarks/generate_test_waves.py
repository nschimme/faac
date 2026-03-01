import wave
import math
import struct
import os
import random

def generate_wave(filename, duration_sec, freq_func, channels=2, rate=44100):
    n_samples = int(duration_sec * rate)
    with wave.open(filename, 'wb') as wf:
        wf.setnchannels(channels)
        wf.setsampwidth(2)
        wf.setframerate(rate)
        chunk_size = 1000
        for i in range(0, n_samples, chunk_size):
            actual_chunk = min(chunk_size, n_samples - i)
            frames = b""
            for j in range(actual_chunk):
                curr_i = i + j
                t = curr_i / float(rate)
                val = freq_func(t, curr_i)
                val = max(-1.0, min(1.0, val))
                sample = int(val * 32767)
                frames += struct.pack('<h', sample) * channels
            wf.writeframes(frames)

def sine_func(t, i):
    return math.sin(2 * math.pi * 440 * t)

def sweep_func(t, i):
    # Logarithmic sweep from 20Hz to 20kHz
    f = 20 * (1000 ** (t / 600.0))
    return math.sin(2 * math.pi * f * t)

def noise_func(t, i):
    return random.uniform(-1.0, 1.0)

def silence_func(t, i):
    return 0.0

if __name__ == "__main__":
    os.makedirs("test_waves", exist_ok=True)
    random.seed(42)
    DURATION = 600
    print(f"Generating 10-minute test waves in 'test_waves/' folder...")
    generate_wave("test_waves/sine.wav", DURATION, sine_func)
    generate_wave("test_waves/sweep.wav", DURATION, sweep_func)
    generate_wave("test_waves/noise.wav", DURATION, noise_func)
    generate_wave("test_waves/silence.wav", DURATION, silence_func)
