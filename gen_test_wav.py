import wave
import math
import struct

def generate_sine_wave(filename, duration=1.0, freq=440.0, sample_rate=44100):
    n_samples = int(duration * sample_rate)
    with wave.open(filename, 'w') as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(sample_rate)
        for i in range(n_samples):
            sample = int(32767.0 * math.sin(2.0 * math.pi * freq * i / sample_rate))
            f.writeframes(struct.pack('<h', sample))

generate_sine_wave('test.wav')
