import wave
import struct
import math
import random
import os

def generate_wav(filename, duration=120, sample_rate=44100, type='sine'):
    n_samples = duration * sample_rate
    with wave.open(filename, 'w') as f:
        f.setnchannels(2)
        f.setsampwidth(2)
        f.setframerate(sample_rate)

        if type == 'sine':
            for i in range(n_samples):
                val = int(16383 * math.sin(2 * math.pi * 1000 * i / sample_rate))
                f.writeframesraw(struct.pack('<hh', val, val))
        elif type == 'noise':
            random.seed(42)
            for i in range(n_samples):
                val = random.randint(-16384, 16383)
                f.writeframesraw(struct.pack('<hh', val, val))
        elif type == 'sweep':
            for i in range(n_samples):
                freq = 20 + (20000 - 20) * (i / n_samples)
                val = int(16383 * math.sin(2 * math.pi * freq * i / sample_rate))
                f.writeframesraw(struct.pack('<hh', val, val))
        elif type == 'silent':
            f.writeframesraw(struct.pack('<hh', 0, 0) * n_samples)

if __name__ == '__main__':
    if not os.path.exists('baselines'):
        os.makedirs('baselines')

    print("Generating sine.wav...")
    generate_wav('baselines/sine.wav', type='sine')
    print("Generating noise.wav...")
    generate_wav('baselines/noise.wav', type='noise')
    print("Generating sweep.wav...")
    generate_wav('baselines/sweep.wav', type='sweep')
    print("Generating silent.wav...")
    generate_wav('baselines/silent.wav', type='silent')
    print("Done.")
